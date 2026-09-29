#!/usr/bin/env python3
"""Run isolated seven-replica workloads; retain every raw log and per-run CSV."""
import argparse,csv,json,os,pathlib,re,shutil,statistics,subprocess,time,datetime

def percentile(a,p):
    return sorted(a)[round((len(a)-1)*p)] if a else 0

def run_case(result, repo, client, mode, payload, batch, repeat, active, faulty, seconds):
    name=f'{mode}-p{payload}-b{batch}-f{faulty}-a{len(active)}-r{repeat}'
    dest=result/name
    if args.resume and (dest/'metrics.csv').exists():
        records=list(csv.DictReader((dest/'metrics.csv').open()))
        if not records:
            records=[x for x in csv.DictReader((result/'raw.csv').open()) if x['mode']==mode and int(x['payload_bytes'])==payload and int(x['batch'])==batch and int(x['repeat'])==repeat and int(x['nfaulty'])==faulty and x['active']==' '.join(map(str,active))]
        previous=records[-1]
        ok=(int(previous['confirmed'])>0 and int(previous['all_replicas_alive']) and
            int(previous.get('all_replicas_reconstructing','1')) and int(previous['continuous_progress']) and
            not int(previous['hash_failures']) and not int(previous['unsafe_votes']))
        print(name,'RESUME completed PASS' if ok else 'RESUME completed FAIL',flush=True)
        return bool(ok)
    if args.resume and dest.exists():
        dest.rename(result/(name+'.interrupted-'+str(time.time_ns())))
    dest.mkdir()
    conf=dest/'conf'
    shutil.copytree(repo/'benchmarks/conf-7node',conf)
    for p in conf.glob('*.conf'):
        p.write_text(re.sub(r'^block-size = .*$',f'block-size = {batch}',p.read_text(),flags=re.M))
    env=os.environ.copy()
    for key in list(env):
        if key.startswith('HOTSTUFF_'): del env[key]
    env.update(HOTSTUFF_NFAULTY=str(faulty),HOTSTUFF_WIRE_STATS='1')
    full=mode not in ('baseline','compatibility')
    broadcast=mode=='full-broadcast'
    env.update(HOTSTUFF_ERASURE_FULL_COMMANDS=str(int(full)),HOTSTUFF_REQUIRE_COMMAND_RECONSTRUCTION=str(int(full and mode!='shadow')),HOTSTUFF_BENCHMARK_FULL_BROADCAST=str(int(broadcast)),HOTSTUFF_CLIENT_LEADER='0')
    if args.phase in ('correctness','negative'): env['HOTSTUFF_COMMAND_PAYLOAD_BYTES']=str(payload)
    (dest/'environment.json').write_text(json.dumps(env | {'NOTE':'runtime environment recorded; sensitive inherited values excluded below'},indent=2) if False else json.dumps({k:v for k,v in env.items() if k.startswith('HOTSTUFF_')},indent=2))
    processes=[]; handles=[]; all_alive=True; unexpected_exits={}; rss_peak=0; cpu_peak=0.; cpu_seconds=0.; prev_cpu=0.; prev_t=time.monotonic()
    def sample():
        nonlocal all_alive,rss_peak,cpu_peak,cpu_seconds,prev_cpu,prev_t
        rss=0; cpu=0.
        for rid,p in processes:
            if p.poll() is not None:
                all_alive=False;unexpected_exits[rid]=p.returncode;continue
            try:
                status=pathlib.Path(f'/proc/{p.pid}/status').read_text()
                rss+=int(re.search(r'VmRSS:\s+(\d+)',status).group(1))*1024
                st=pathlib.Path(f'/proc/{p.pid}/stat').read_text().split()
                cpu+=(int(st[13])+int(st[14]))/os.sysconf('SC_CLK_TCK')
            except (FileNotFoundError,AttributeError): all_alive=False
        now=time.monotonic();rss_peak=max(rss_peak,rss)
        cpu_peak=max(cpu_peak,100*max(0,cpu-prev_cpu)/max(.001,now-prev_t))
        cpu_seconds=max(cpu_seconds,cpu);prev_cpu=cpu;prev_t=now
    start=None; cp=None
    try:
        for rid in active:
            h=(dest/f'replica{rid}.log').open('w');handles.append(h)
            p=subprocess.Popen([str(repo/'examples/hotstuff-app'),'--conf',f'hotstuff-sec{rid}.conf'],cwd=conf,env=env,stdout=h,stderr=subprocess.STDOUT)
            processes.append((rid,p))
        until=time.monotonic()+3
        while time.monotonic()<until: sample();time.sleep(.1)
        h=(dest/'client.log').open('w');handles.append(h)
        start=time.monotonic(); start_epoch=time.time()
        cp=subprocess.Popen([str(client),'--conf','hotstuff-7.conf','--idx','0','--iter','-1','--max-async',str({1:16,16:64,64:128}[batch])],cwd=conf,env=env,stdout=h,stderr=subprocess.STDOUT)
        while time.monotonic()-start<seconds:
            sample()
            if cp.poll() is not None: break
            time.sleep(.1)
        sample();elapsed=time.monotonic()-start
    finally:
        if cp and cp.poll() is None:
            cp.terminate()
            try:cp.wait(5)
            except subprocess.TimeoutExpired:cp.kill();cp.wait()
        for _,p in processes:
            if p.poll() is None:p.terminate()
        for _,p in processes:
            try:p.wait(5)
            except subprocess.TimeoutExpired:p.kill();p.wait()
        for h in handles:h.close()
    ctxt=(dest/'client.log').read_text(errors='replace')
    seen={}
    for h,lat in re.findall(r'benchmark_confirmed hash=(\w+) latency=([\d.]+)',ctxt):seen[h]=float(lat)
    latencies=list(seen.values())
    # New client logs retain full hashes; refuse silently substituting old truncated output.
    wire=re.compile(r'wire_tx opcode=(\d+) bytes=(\d+)')
    tx=lambda t:sum(int(b) for _,b in wire.findall(t))
    cluster=0;proposal=0;reproposal=0;leader_total=0;reconstructed=0;hash_failures=0;unsafe_votes=0;decode=[];encode=[];min_recon=None;follower_body=0;replica_progress=[]
    all_text=[]
    for rid,_ in processes:
        text=(dest/f'replica{rid}.log').read_text(errors='replace');all_text.append(text)
        cluster+=tx(text)
        if rid==0:leader_total=tx(text)
        proposal+=sum(map(int,re.findall(r'proposal_direct_wire_bytes: (\d+)',text)))
        if mode=='baseline':proposal+=sum(map(int,re.findall(r'\] proposal_wire_bytes: (\d+)',text)))
        reproposal+=sum(map(int,re.findall(r'reproposal_wire_bytes: (\d+)',text)))
        count=text.count('body_reconstructed:');reconstructed+=count
        reconstruction_times=[]
        for line in text.splitlines():
            if 'body_reconstructed:' in line:
                try:reconstruction_times.append(datetime.datetime.fromisoformat(line[:26]).timestamp()-start_epoch)
                except ValueError:pass
        replica_progress.append(bool(reconstruction_times) and max(reconstruction_times)>elapsed-4)
        min_recon=count if min_recon is None else min(count,min_recon)
        hash_failures+=text.count('body_hash_failure:')
        if rid!=0:follower_body+=sum(map(int,re.findall(r'body_client_received: (\d+)',text)))
        known=set()
        for line in text.splitlines():
            match=re.search(r'body_reconstructed: origin=(\w+)',line)
            if match:known.add(match.group(1))
            match=re.search(r'body_vote: origin=(\w+)',line)
            if match and match.group(1) not in known:unsafe_votes+=1
        decode+=list(map(int,re.findall(r'rs_decode_us=(\d+)',text)))
        encode+=list(map(int,re.findall(r'rs_encode_us: (\d+)',text)))
    confirmation_times=[]
    for line in ctxt.splitlines():
        if 'benchmark_confirmed hash=' in line:
            try: confirmation_times.append(datetime.datetime.fromisoformat(line[:26]).timestamp()-start_epoch)
            except ValueError: pass
    last_confirmation=max(confirmation_times,default=0)
    progressing=int(bool(confirmation_times) and last_confirmation>elapsed-4 and len({int(t//5) for t in confirmation_times})>=max(1,int(elapsed//5)-1))
    decode_only=sum(int(x) for t in all_text for x in re.findall(r'rs_decode_only_us: (\d+)',t))
    framed_proposal=sum(int(b) for t in all_text for op,b in wire.findall(t) if int(op)==0)
    framed_reproposal=sum(int(b) for t in all_text for op,b in wire.findall(t) if int(op)==7)
    client_wire=tx(ctxt)
    body_sent=sum(map(int,re.findall(r'client_body_bytes: (\d+)',ctxt)))
    confirmed=len(seen)
    per=lambda value:value/confirmed if confirmed else 0
    row=dict(mode=mode,payload_bytes=payload,batch=batch,repeat=repeat,nfaulty=faulty,active=' '.join(map(str,active)),duration_s=round(elapsed,6),confirmed=confirmed,throughput=confirmed/elapsed,
        mean_latency=statistics.mean(latencies) if latencies else 0,p50=percentile(latencies,.5),p95=percentile(latencies,.95),
        leader_proposal_bytes=framed_proposal,reproposal_bytes=framed_reproposal,proposal_message_body_bytes=proposal,reproposal_message_body_bytes=reproposal,leader_all_wire_bytes=leader_total,cluster_all_wire_bytes=cluster,client_body_bytes=body_sent,client_all_wire_bytes=client_wire,
        end_to_end_bytes=cluster+client_wire,bytes_per_confirmed=per(cluster+client_wire),leader_proposal_bytes_per_confirmed=per(framed_proposal),leader_all_wire_bytes_per_confirmed=per(leader_total),reproposal_bytes_per_confirmed=per(framed_reproposal),cluster_all_wire_bytes_per_confirmed=per(cluster),client_body_bytes_per_confirmed=per(body_sent),client_all_wire_bytes_per_confirmed=per(client_wire),
        rs_encode_us=sum(encode),rs_decode_us=decode_only,continuous_progress=progressing,last_confirmation_s=last_confirmation,rs_decode_verify_us=sum(decode),cpu_seconds=cpu_seconds,cpu_peak_percent=cpu_peak,replica_rss_peak_bytes=rss_peak,
        reconstructed=reconstructed,all_replicas_reconstructing=int(all(replica_progress)) if full else 1,min_replica_reconstructions=min_recon or 0,hash_failures=hash_failures,unsafe_votes=unsafe_votes,follower_client_body_bytes=follower_body,all_replicas_alive=int(all_alive),unexpected_replica_exit_codes=json.dumps(unexpected_exits,sort_keys=True),replica_shutdown_exit_codes=json.dumps({rid:p.returncode for rid,p in processes},sort_keys=True),client_exit=cp.returncode)
    with (dest/'metrics.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=row);w.writeheader();w.writerow(row)
    with (result/'raw.csv').open('a',newline='') as f:
        w=csv.DictWriter(f,fieldnames=row)
        if f.tell()==0:w.writeheader()
        w.writerow(row)
    no_reproposal=mode!='full-broadcast' or (framed_reproposal==0 and reproposal==0)
    no_rs=mode!='full-broadcast' or (not encode and decode_only==0)
    ok=confirmed>0 and progressing and all_alive and not hash_failures and not unsafe_votes and no_reproposal and no_rs and (not full or (all(replica_progress) and min_recon and (mode=='shadow' or not follower_body)))
    print(name,'PASS' if ok else ('EXPECTED_NO_PROGRESS' if args.phase=='negative' else 'FAIL'),f'confirmed={confirmed} alive={all_alive} hash_failures={hash_failures}',flush=True)
    return ok

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--phase',choices=['correctness','performance','negative'],required=True);ap.add_argument('--out',type=pathlib.Path,required=True);ap.add_argument('--payload',type=int,default=1024);ap.add_argument('--seconds',type=int,default=20);ap.add_argument('--repetitions',type=int,default=3)
    ap.add_argument('--baseline',type=pathlib.Path);ap.add_argument('--resume',action='store_true');args=ap.parse_args();args.out.mkdir(exist_ok=True,parents=True)
    root=pathlib.Path.home()/'librightstuff';baseline=args.baseline or pathlib.Path.home()/'librightstuff-baseline'
    if subprocess.run(['pgrep','-f','hotstuff-app --conf'],stdout=subprocess.DEVNULL).returncode==0:raise SystemExit('Existing replicas running')
    good=True
    if args.phase=='correctness':
        for mode,active,f in [('shadow',list(range(7)),2),('strict',list(range(7)),2),('strict',[0,3,4,5,6],2),('strict',[0,4,5,6],3),('full-broadcast',list(range(7)),2)]:
            for repeat in range(1,args.repetitions+1):
                good=run_case(args.out,root,root/'examples/hotstuff-client',mode,args.payload,16,repeat,active,f,args.seconds) and good
    elif args.phase=='negative':
        run_case(args.out,root,root/'examples/hotstuff-client','strict',args.payload,16,1,[0,6],2,10)
        row=list(csv.DictReader((args.out/'raw.csv').open()))[-1]
        votes=sum(p.read_text().count('body_vote:') for p in args.out.glob('*/replica*.log'))
        good=int(row['confirmed'])==0 and int(row['reconstructed'])==0 and votes==0 and int(row['all_replicas_alive'])==1
        print('insufficient-shards negative test', 'PASS' if good else 'FAIL',flush=True)
    else:
        for batch in [1,16,64]:
            for repeat in range(1,args.repetitions+1):
                for mode,repo in [('baseline',baseline),('full-broadcast',root),('strict',root)]:
                    good=run_case(args.out,repo,root/'examples/hotstuff-client',mode,args.payload,batch,repeat,list(range(7)),3,30) and good
    (args.out/'DONE.json').write_text(json.dumps({'passed':good}))
    raise SystemExit(0 if good else 1)
