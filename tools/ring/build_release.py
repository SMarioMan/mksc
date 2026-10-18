#!/usr/bin/env python3
"""Build immutable title/direct-boot audio variants and a relocation fixture."""
from pathlib import Path
import argparse
import hashlib,json,shutil,subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',required=True,type=Path)
p.add_argument('--sunset-art',choices=('original','japanese'),default='original')
p.add_argument('--music-restoration-ips',type=Path)
a=p.parse_args();
if a.music_restoration_ips:a.music_restoration_ips=a.music_restoration_ips.resolve()
audio_args=['--music-restoration-ips',str(a.music_restoration_ips)] if a.music_restoration_ips else []
root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
subprocess.run(['make','-j4'],cwd=root,check=True)
def publish(source,target):
 # Replacing the inode preserves ROMs already mmap'ed by a running emulator.
 temporary=target.with_name(target.name+'.tmp')
 shutil.copyfile(source,temporary);temporary.replace(target)
variants={}
for boot in (False,True):
 for music,label in (('normal',''),('shared','-shared')):
  name='mksc-ring'+('-boot' if boot else '')+label
  with (out/(name+'-build.log')).open('w') as log:
   subprocess.run(['python3','tools/ring/build.py','--music',music,'--sunset-art',a.sunset_art]+audio_args+(['--boot-ring'] if boot else []),cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
  for ext in ('gba','elf','sym','map'):publish(root/('mksc-ring.'+ext),out/(name+'.'+ext))
  variants[name]={'boot_ring':boot,'music':music,'sunset_art':a.sunset_art,'music_restoration_sha256':hashlib.sha256(a.music_restoration_ips.read_bytes()).hexdigest() if a.music_restoration_ips else None,'sha256':hashlib.sha256((out/(name+'.gba')).read_bytes()).hexdigest()}
  (out/'variants.json').write_text(json.dumps(variants,indent=2)+'\n')
  print('BUILT',name,variants[name]['sha256'],flush=True)
with (out/'shifted-build.log').open('w') as log:
 subprocess.run(['python3','tools/ring/build.py','--code-padding','0x400','--sunset-art',a.sunset_art]+audio_args,cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
for ext in ('gba','elf','sym','map'):publish(root/('mksc-ring.'+ext),out/('mksc-ring-shifted.'+ext))
# Keep the ordinary working filenames useful for manual testing. The shifted
# relocation fixture has its own explicit name in the release directory.
for ext in ('gba','elf','sym','map'):publish(out/('mksc-ring.'+ext),root/('mksc-ring.'+ext))
assert hashlib.sha1((root/'mksc.gba').read_bytes()).hexdigest()=='9d327c030c3e2d9007990518594f70c3340ac56f'
print('PASS four variants, shifted build, and stock hash',flush=True)
