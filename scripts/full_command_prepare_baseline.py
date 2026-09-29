from pathlib import Path
import subprocess,tarfile,tempfile,shutil,json
home=Path.home();source=home/'librightstuff-baseline'
target=Path(tempfile.mkdtemp(prefix='full-command-original-baseline-',dir=home))
archive=target/'source.tar'
with archive.open('wb') as f:subprocess.run(['git','-C',str(source),'archive','HEAD'],stdout=f,check=True)
with tarfile.open(archive) as t:t.extractall(target)
archive.unlink()
for name in ['salticidae','secp256k1']:
    # git archive contains empty submodule directories only.
    shutil.copytree(source/name,target/name,dirs_exist_ok=True)
shutil.copytree(source/'benchmarks/conf-7node',target/'benchmarks/conf-7node')
# Apply only message statistics to the original baseline dependency.
network=target/'salticidae/include/salticidae/network.h'
text=network.read_text()
if 'wire_tx opcode=' not in text:
    marker='    return conn->write(std::move(msg_data));'
    assert marker in text
    network.write_text(text.replace(marker,"""    const size_t wire_bytes=msg_data.size();
    const bool accepted=conn->write(std::move(msg_data));
    if(accepted && std::getenv("HOTSTUFF_WIRE_STATS"))
        SALTICIDAE_LOG_INFO("wire_tx opcode=%u bytes=%lu",unsigned(msg.get_opcode()),wire_bytes);
    return accepted;"""))

p=target/'include/hotstuff/hotstuff.h';s=p.read_text();a='        _do_broadcast<Proposal, MsgPropose>(prop);';assert a in s
p.write_text(s.replace(a,'''        MsgPropose msg(prop);
        HOTSTUFF_LOG_INFO("proposal_wire_bytes: %lu",msg.serialized.size()*peers.size());
        pn.multicast_msg(std::move(msg),peers);'''))
manifest={'source_commit':subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip(),'path':str(target),'protocol_changes':'none; original quorum=(n+1)/2; byte-statistics-only header modification; shared deterministic workload client from modified repository','payload_format':'original fixed CommandDummy cid+n+REQSIZE; no runtime dynamic format'}
(target/'BASELINE_MANIFEST.json').write_text(json.dumps(manifest,indent=2))
(home/'full-command-baseline-path.txt').write_text(str(target))
print(json.dumps(manifest))
