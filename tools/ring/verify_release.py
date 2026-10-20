#!/usr/bin/env python3
"""Run the real-BIOS release gates, then optional HLE after every gate passes.

Build immutable variants with build_release.py first. The Qt build must include
private GUI tests. Outputs contain private ROM copies and recordings; do not
commit the results directory or redistribute the BIOS/base ROM.
"""
from pathlib import Path
import argparse
from concurrent.futures import ThreadPoolExecutor,as_completed
import hashlib,json,os,re,shutil,subprocess,threading,time,traceback
p=argparse.ArgumentParser(description=__doc__)
for name in ('runner','bios','results','builds','mgba-build','mgba-source'):
 p.add_argument('--'+name,required=True,type=lambda value:Path(value).resolve())
a=p.parse_args()
root=Path(__file__).resolve().parents[2];r=a.results;build=a.builds;runner=a.runner;bios=a.bios
r.mkdir(parents=True,exist_ok=True)
(r/'verification-complete.json').unlink(missing_ok=True)
(r/'completed-checks.json').write_text('[]\n')
variants=json.loads((build/'variants.json').read_text())
assert len(variants)==4,'Build all four variants first'
for name,variant in variants.items():
 assert variant.get('automatic_maps') is True and variant.get('ring_consoles')==16 and variant.get('racer_slots')==8, 'Every ring image must include automatic maps'
 assert hashlib.sha256((build/(name+'.gba')).read_bytes()).hexdigest()==variant['sha256'],name
assert hashlib.sha1(bios.read_bytes()).hexdigest()=='300c20df6731a33952ded8c436f7f186d25d3492'
assert hashlib.sha1((root/'baserom.gba').read_bytes()).hexdigest()=='9d327c030c3e2d9007990518594f70c3340ac56f'
assert hashlib.sha1((root/'mksc.gba').read_bytes()).hexdigest()=='9d327c030c3e2d9007990518594f70c3340ac56f','Clean build no longer matches the stock ROM'
entry=threading.Event();entryOK=False;passed=[];lock=threading.Lock()
def run(name,cmd,env=None):
 print('START',name,flush=True);t=time.time()
 with (r/(name+'.log')).open('w') as log:
  code=subprocess.run([str(x) for x in cmd],cwd=root,stdout=log,stderr=subprocess.STDOUT,env=env).returncode
 if code:raise RuntimeError(f'{name} failed ({code}); see {r/(name+".log")}')
 with lock:
  passed.append({'name':name,'seconds':round(time.time()-t,2),'command':[str(x)for x in cmd]});(r/'completed-checks.json').write_text(json.dumps(passed,indent=2)+'\n')
 print('PASS',name,round(time.time()-t,1),'seconds',flush=True)
def test(stem,name=None,variant='mksc-ring',extra=(),elf=True,unit=False):
 name=name or stem;cmd=['python3',root/'tools/ring'/('test_'+stem+'.py'),'--runner',runner,'--bios',bios,'--results',r/name]
 if not unit:cmd+=['--rom',build/(variant+'.gba')]
 if elf and not unit:cmd+=['--elf',build/(variant+'.elf')]
 if stem=='sunset_art':cmd+=['--sunset-art',variants['mksc-ring'].get('sunset_art','japanese')]
 run(name,cmd+list(extra))
def matrix(policy):
 variant='mksc-ring'+('-shared' if policy=='shared' else '')
 test('all_courses','all-48-'+policy,variant,extra=('--audio-policy',policy,'--complete','--workers','1'))
 d=json.loads((r/('all-48-'+policy)/'summary.json').read_text());assert len(d['passed'])==48 and not d['failures']
def logic():
 global entryOK
 try:
  test('game','entry',extra=('--players','2','3','4','5','6','7','8','--modes','vs','battle','gp'),elf=False);entryOK=True
 finally:entry.set()
 for stem,elf in [('title',True),('lobby',True),('singleplayer',True),('rendering',False),('display_rate',False),('spawns',True),('course_menu',False),('battle_menu',True),('transition',True),('character_menu',True),('character_random',True),('cc_palette',False)]:
  test(stem,elf=elf,extra=())
 test('lobby','lobby-shared',variant='mksc-ring-shared')
 test('gp',extra=('--players','2','3','4','5','6','7','8'))
 for policy in ('normal','shared'):
  variant='mksc-ring'+('-shared' if policy=='shared' else '')
  test('map_display','maps-'+policy,variant,extra=('--workers','1'))
 for count in (9,16):
  test('map_display','maps-rotated-'+str(count),extra=('--consoles',str(count),'--host',str(count-1),'--modes','vs','--workers','1'))
 test('rounds','maps-results',variant='mksc-ring-shared',extra=('--players','8','--consoles','16','--workers','1','--shared-audio','--results-frames','900'))
 test('gp','maps-gp',variant='mksc-ring-shared',extra=('--players','8','--consoles','16'))
 test('rounds','toad-150cc-retro',variant='mksc-ring-shared',extra=('--players','8','--modes','vs','--workers','1','--toad-150cc-retro','--shared-audio'))
 test('flows',extra=('--players','2','8'))
 test('rounds',extra=('--players','2','3','4','5','6','7','8','--workers','1'))
 test('rounds','rounds-shared',variant='mksc-ring-shared',extra=('--players','2','3','4','5','6','7','8','--workers','1','--shared-audio'))
 test('rounds','results-sustained',extra=('--players','2','8','--workers','1','--results-frames','900'))
 test('rounds','results-sustained-shared',variant='mksc-ring-shared',extra=('--players','2','8','--workers','1','--results-frames','900','--shared-audio'))
 test('repeat',extra=('--players','2','8','--baseline',r/'rounds'))
 test('menu_audio',variant='mksc-ring-shared',extra=('--host','3'))
 test('start_music',variant='mksc-ring-boot-shared',extra=('--boot-ring','--host','3','--reference-rom',build/'mksc-ring-boot.gba','--reference-elf',build/'mksc-ring-boot.elf'))
 test('finish_music',variant='mksc-ring-shared')
 test('sunset_art')
 test('music','music-leader',variant='mksc-ring-boot-shared',extra=('--boot-ring','--policy','shared','--host','3','--gp','--retro','--require-shrink'))
 test('audio','audio-shared',variant='mksc-ring-boot-shared',extra=('--boot-ring','--gp','--audio-policy','shared'))
 test('audio','audio-normal',extra=('--frames','24000','--audio-policy','normal'))
 test('gp_cup',extra=('--players','2','3','4','5','6','7','8','--autofire'))
 test('gp_cup','retro-special-cup-maps',variant='mksc-ring-shared',extra=('--players','8','--consoles','9','--cup','4','--engine','1','--bowser-p4','--autofire','--video-log','--results-frames','900'))
 run('relocation',['python3',root/'tools/ring/audit_relocation.py','--rom',build/'mksc-ring.gba','--elf',build/'mksc-ring.elf','--shifted-rom',build/'mksc-ring-shifted.gba','--shifted-elf',build/'mksc-ring-shifted.elf'])
 test('singleplayer','shifted-singleplayer',variant='mksc-ring-shifted',extra=())
 test('game','shifted-game',variant='mksc-ring-shifted',elf=False,extra=('--players','2','8','--modes','vs','battle','gp'))
 test('battle_menu','shifted-retro-battle-menu',variant='mksc-ring-shifted')
 test('spawns','shifted-retro-battle-spawns',variant='mksc-ring-shifted',extra=('--players','2','8','--sets','retro'))
 test('sunset_art','shifted-sunset-art',variant='mksc-ring-shifted',extra=('--players','2'))
def gui_units():
 entry.wait()
 if not entryOK:raise RuntimeError('GUI requires successful private input fixtures')
 env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_RING_GUI_')};env.update(QT_QPA_PLATFORM='offscreen',SDL_AUDIODRIVER='dummy',MGBA_RING_GUI_ROM=str(build/'mksc-ring.gba'),MGBA_RING_GUI_BOOT_ROM=str(build/'mksc-ring-boot-shared.gba'),MGBA_RING_GUI_BIOS=str(bios),MGBA_RING_GUI_INPUTS=str(r/'entry'),MGBA_RING_GUI_STOCK_INPUTS=str(a.mgba_source/'tools/link/inputs'))
 run('ctest-private',['ctest','--test-dir',a.mgba_build,'--output-on-failure','--parallel','1'],env)
 shutil.copyfile(a.mgba_build/'Testing/Temporary/LastTest.log',r/'ctest-private-detail.log')
 detail=(r/'ctest-private-detail.log').read_text()
 rows=set(re.findall(r'PASS\s*:\s*RingGuiTest::([^\n]+)',detail))
 required={'scriptedLaunch()','threadedVideoReset()','controllerSlots()','ringArguments()'}|{f'ring({row})' for row in ('vs-2','battle-2','vs-8','battle-8','vs-8-loss-start','vs-2-late','stock-vs-4','stock-battle-4')}
 assert required<=rows,('Private GUI checks did not execute',sorted(required-rows))
 assert not re.search(r'SKIP\s*:\s*RingGuiTest::',detail),'Private GUI checks must not skip'
 env['MGBA_RING_GUI_PLAYERS']='16'
 run('gui-sixteen-consoles',[a.mgba_build/'qt/test-qt-ring_gui','scriptedLaunch'],env)
 env['MGBA_RING_GUI_TITLE_ENTRY']='1'
 run('gui-sixteen-title-entry',[a.mgba_build/'qt/test-qt-ring_gui','scriptedLaunch'],env)
 for stem in ('projection','oam_cell','identity_atlas','collision','sound_volume','sound_control','shared_themes','target','camera','animation'):
  test(stem,unit=True)
 for ahead in (2,3):test('audio_queue','audio-queue-'+str(ahead),unit=True,extra=('--ahead',str(ahead)))
 for stem in ('inputs','divide','projection_divide'):run(stem,['python3',root/'tools/ring'/('test_'+stem+'.py')])
 run('hot-code',['python3',root/'tools/ring/test_hot_code.py','--elf',build/'mksc-ring.elf'])
 run('stock',['python3',a.mgba_source/'tools/link/test_stock.py','--runner',runner,'--rom',root/'baserom.gba','--bios',bios,'--results',r/'stock'])
 inputs=r/'all-48-normal/super-14.csv'
 while not inputs.exists():time.sleep(1)
 test('profile',extra=('--inputs',inputs))
 test('threaded_video',elf=False,extra=('--inputs',r/'entry/vs-8.csv'))
def gui_audio():
 # Host-clock delivery needs a real audio server and an otherwise idle host.
 # Keep it outside the concurrent native-cycle checks and all builds.
 env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_RING_GUI_')};env.update(QT_QPA_PLATFORM='offscreen',MGBA_RING_GUI_ROM=str(build/'mksc-ring-shared.gba'),MGBA_RING_GUI_BIOS=str(bios),MGBA_RING_GUI_INPUTS=str(r/'entry'),MGBA_RING_GUI_AUDIO_FRAMES='18000')
 for backend in ('1','qt'):
  env['MGBA_RING_GUI_AUDIO']=backend
  run('gui-audio-'+('sdl' if backend=='1' else 'qt'),[a.mgba_build/'qt/test-qt-ring_gui','ring:vs-8'],env)
 env.update(MGBA_RING_GUI_AUDIO='qt',MGBA_RING_GUI_AUDIO_FRAMES='1900')
 run('gui-audio-qt-topology-two',[a.mgba_build/'qt/test-qt-ring_gui','ring:vs-2'],env)
 env['MGBA_RING_GUI_SAMPLE_RATE']='192000'
 run('gui-audio-qt-high-rate-two',[a.mgba_build/'qt/test-qt-ring_gui','ring:vs-2'],env)
# New authored starts change the driving fixture used to lose balloons.
# Check that coverage before spending hours on the full course matrix.
test('battle_survivors')
with ThreadPoolExecutor(max_workers=3) as pool:
 futures=[pool.submit(matrix,'normal'),pool.submit(matrix,'shared'),pool.submit(logic)]
 failures=[]
 for f in as_completed(futures):
  try:f.result()
  except Exception:
   failures.append(traceback.format_exc());print(failures[-1],flush=True)
if failures:raise SystemExit('BIOS validation failed; HLE deliberately not run')
gui_units()
gui_audio()
print('PASS ALL BIOS CHECKS; starting optional HLE last',flush=True)
test('game','hle-game',elf=False,extra=('--hle','--players','2','8','--modes','vs','battle','gp'))
test('rounds','hle-rounds',extra=('--hle','--players','2','8','--workers','1'))
test('singleplayer','hle-singleplayer',extra=('--hle',))
(r/'verification-complete.json').write_text(json.dumps({'bios':str(bios),'tests':passed,'hle_after_bios':True},indent=2)+'\n')
print('PASS ALL BIOS AND HLE CHECKS',flush=True)
