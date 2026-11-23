#!/usr/bin/env python3
"""Run the built platform probe with local extracted worlds on an isolated SD."""
from pathlib import Path
import argparse,os,shutil,subprocess,sys,time
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--missing-scene',action='store_true');parser.add_argument('--runtime',action='store_true');parser.add_argument('--menu',action='store_true');parser.add_argument('--effects',action='store_true');parser.add_argument('--buttons',action='store_true');parser.add_argument('--features',action='store_true');parser.add_argument('--stereo-bottom',action='store_true');parser.add_argument('--skip-intro',action='store_true');parser.add_argument('--wait',type=float,default=15);args=parser.parse_args()
if not 1 <= args.wait <= 55: parser.error('--wait must be between 1 and 55 seconds')
ROOT=Path(__file__).resolve().parents[2];out=ROOT/('build/3ds/emulator-runtime' if args.runtime else 'build/3ds/emulator-missing' if args.missing_scene else 'build/3ds/emulator');out.mkdir(parents=True,exist_ok=True)
if os.environ.get('THIEVIUS_PROBE_DIR'):
    out=Path(os.environ['THIEVIUS_PROBE_DIR']).resolve()
    out.mkdir(parents=True,exist_ok=True)
if args.runtime:
    shutil.rmtree(out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR/Saves',ignore_errors=True)
    for logname in ('runtime.log','runtime-stderr.log'):
        (out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR'/logname).unlink(missing_ok=True)
sdmc_root=out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR'
sdmc_root.mkdir(parents=True,exist_ok=True)
def link_asset_tree(name):
    source=ROOT/'local/data'/name
    if name=='Cutscenes' and os.environ.get('THIEVIUS_FMV_TEST_DIR'):
        source=Path(os.environ['THIEVIUS_FMV_TEST_DIR']).resolve()
    if not source.exists():return
    target=sdmc_root/name
    if target.is_symlink():
        if target.resolve()==source.resolve():return
        target.unlink()
    elif target.is_dir():
        shutil.rmtree(target)
    elif target.exists():
        target.unlink()
    target.symlink_to(source)
link_asset_tree('Worlds')
link_asset_tree('Cutscenes')
if args.runtime:
    for opt_name in ('extraction_manifest.json','assets-ready.stamp'):
        opt_src=ROOT/'local/data'/opt_name
        if opt_src.is_file():shutil.copy2(opt_src,sdmc_root/opt_name)
scene=ROOT/'local/data/Paris.cscene'
if scene.exists() and not args.missing_scene:shutil.copy2(scene,sdmc_root/'Paris.cscene')
icon_cfg=out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR/native-options.cfg'
cfg_lines=icon_cfg.read_text().splitlines() if icon_cfg.exists() else []
if args.skip_intro:
    cfg_lines=[line for line in cfg_lines if line.split(' ',1)[0] not in ('intro_seen','skip_intro')]+['intro_seen 1','skip_intro 1']
    icon_cfg.write_text('\n'.join(cfg_lines)+'\n')
if args.stereo_bottom:
    if not args.runtime or args.menu:parser.error('--stereo-bottom needs --runtime without --menu')
    keys={'display_mode':'6','bottom_coins':'1','bottom_health':'1','bottom_monitor':'0'}
    cfg_lines=[line for line in cfg_lines if line.split(' ',1)[0] not in keys]+[f'{key} {value}' for key,value in keys.items()]
    icon_cfg.write_text('\n'.join(cfg_lines)+'\n')
if args.runtime and not args.menu and icon_cfg.exists():
    defaults={'swap_ba':'1','camera_invert_x':'1','camera_invert_y':'0','binoc_invert_x':'0','binoc_invert_y':'1'}
    cleaned=[line.rsplit(' ',1)[0]+' '+defaults[line.rsplit(' ',1)[0]] if line.rsplit(' ',1)[0] in defaults else line for line in cfg_lines]
    if cleaned!=cfg_lines:
        icon_cfg.write_text('\n'.join(cleaned)+'\n')
        cfg_lines=icon_cfg.read_text().splitlines()
initial_icons='PS2' if 'button_icons 0' in cfg_lines else 'New 3DS'
initial_swap='Off' if 'swap_ba 0' in cfg_lines else 'On'
initial_inverts=(
    0 if 'camera_invert_x 0' in cfg_lines else 1,
    1 if 'camera_invert_y 1' in cfg_lines else 0,
    1 if 'binoc_invert_x 1' in cfg_lines else 0,
    0 if 'binoc_invert_y 0' in cfg_lines else 1,
)
if args.features:
    if not (args.runtime and args.menu and args.buttons):parser.error('--features needs --runtime --menu --buttons')
    keys={'display_mode':'0','stereo_strength':'100','gyro_enabled':'0','gyro_sensitivity':'100','low_shadow_detail':'0','bottom_coins':'1','bottom_health':'1','bottom_monitor':'0'}
    lines=[line for line in cfg_lines if line.split(' ',1)[0] not in keys]
    icon_cfg.write_text('\n'.join(lines+[f'{key} {value}' for key,value in keys.items()])+'\n')
    qt=out/'config/azaharplus-emu/qt-config.ini'
    if qt.exists():
        import re
        content=qt.read_text()
        content=re.sub(r'^factor_3d=.*$', 'factor_3d=100',content,flags=re.M)
        content=content.replace('factor_3d\\default=true','factor_3d\\default=false')
        qt.write_text(content)
env={**os.environ,'DISPLAY':':95','XDG_CONFIG_HOME':str(out/'config'),'XDG_DATA_HOME':str(out/'data'),'QT_X11_NO_MITSHM':'1','LIBGL_ALWAYS_SOFTWARE':'1'}
with (out/'xvfb.log').open('w') as xlog,(out/'azahar.log').open('w') as log:
    x=subprocess.Popen(['Xvfb',':95','-screen','0','1000x900x24'],stdout=xlog,stderr=xlog)
    emulator=None
    try:
        time.sleep(2)
        if x.poll() is not None:raise RuntimeError('Display :95 is unavailable')
        emulator=subprocess.Popen(['azahar','-w',str(ROOT/('build/3ds/ThieviusCTR.3dsx' if args.runtime else 'build/3ds/ThieviusCTR-viewer.3dsx'))],env=env,stdout=log,stderr=log)
        if args.runtime:
            runtime_log=out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR/runtime.log'
            intro_captured=False
            deadline=time.monotonic()+max(int(args.wait),600)
            while time.monotonic()<deadline:
                if emulator.poll() is not None:raise RuntimeError('Emulator exited before capture')
                text=runtime_log.read_text() if runtime_log.exists() else ''
                if 'probe stopped' in text or 'abort code=' in text:raise RuntimeError(text.splitlines()[-1])
                if not intro_captured and 'Playing original startup video' in text and 'ui=3' in text:
                    subprocess.run(['import','-window','root',str(out/'intro-leap.png')],env=env,check=True)
                    intro_captured=True
                if 'Original title menu:' in text:
                    time.sleep(2)
                    break
                time.sleep(1)
            else:
                tail='\n'.join(text.splitlines()[-20:])
                raise RuntimeError('Timed out waiting for Original title menu:\n'+tail)
        else:
            time.sleep(args.wait)
        if emulator.poll() is not None:raise RuntimeError('Emulator exited before capture')
        subprocess.run(['import','-window','root',str(out/'boot.png')],env=env,check=True)
        print('Captured:',out/'boot.png',flush=True)
        windows=subprocess.check_output(['xdotool','search','--onlyvisible','--class','[Aa]zahar.*'],env=env,text=True).split()
        if windows:
            subprocess.run(['xdotool','windowfocus',windows[-1]],env=env,check=True)
            if args.menu:
                swap_on=(initial_swap=='On')
                def confirm_key():
                    return 'a' if swap_on else 's'
                def tap(key,seconds=.15):
                    subprocess.run(['xdotool','keydown',key],env=env,check=True);time.sleep(seconds)
                    subprocess.run(['xdotool','keyup',key],env=env,check=True);time.sleep(1)
                tap('n');time.sleep(1)
                tap('g');tap('g');tap(confirm_key());time.sleep(2)
                subprocess.run(['import','-window','root',str(out/'options.png')],env=env,check=True)
                tap('h');time.sleep(2)
                subprocess.run(['import','-window','root',str(out/'ndsp-fallback.png')],env=env,check=True)
                tap('h');tap('g');tap('g');tap('f')
                subprocess.run(['import','-window','root',str(out/'sound-volume.png')],env=env,check=True)
                tap('z');tap('z');tap('n');time.sleep(2)
                subprocess.run(['import','-window','root',str(out/'original-options.png')],env=env,check=True)
                if args.buttons:
                    tap('g');tap('g');tap('g');tap(confirm_key());time.sleep(2)
                    if initial_icons=='PS2':tap('h')
                    subprocess.run(['import','-window','root',str(out/'buttons-new-3ds.png')],env=env,check=True)
                    tap('h')
                    subprocess.run(['import','-window','root',str(out/'buttons-ps2.png')],env=env,check=True)
                    cfg=out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR/native-options.cfg'
                    if cfg.read_text().splitlines()[0]!='button_icons 0':raise RuntimeError('PS2 choice not saved')
                    tap('h')
                    if cfg.read_text().splitlines()[0]!='button_icons 1':raise RuntimeError('New 3DS choice not saved')
                    tap(confirm_key());tap(confirm_key())
                    if cfg.read_text().splitlines()[0]!='button_icons 1':raise RuntimeError('Confirm did not toggle icon choice')
                    if args.features:
                        def saved(line):
                            if line not in cfg.read_text().splitlines():raise RuntimeError('Setting not saved: '+line)
                        tap('g');tap(confirm_key());tap('h');saved('display_mode 1');time.sleep(2)
                        subprocess.run(['import','-window','root',str(out/'display-800.png')],env=env,check=True)
                        tap('h');saved('display_mode 2');time.sleep(2)
                        subprocess.run(['import','-window','root',str(out/'display-stereo.png')],env=env,check=True)
                        for mode,name in ((3,'ssaa'),(4,'picasr-aa'),(5,'picasr-sharpen'),(6,'stereo-480'),(0,'native')):
                            tap('h');saved('display_mode '+str(mode));time.sleep(2)
                            subprocess.run(['import','-window','root',str(out/('display-'+name+'.png'))],env=env,check=True)
                        # Compare the fixed title/first menu row against the
                        # known upright SSAA path, not just display-mode logs.
                        from PIL import Image,ImageChops,ImageStat
                        def top_image(path):return Image.open(path).convert('L').crop((300,225,700,465))
                        reference=top_image(out/'display-ssaa.png').crop((110,52,290,89)).point(lambda x:255 if x>135 else 0)
                        transforms=(None,Image.Transpose.ROTATE_180,Image.Transpose.FLIP_TOP_BOTTOM,Image.Transpose.FLIP_LEFT_RIGHT)
                        for name in ('picasr-aa','picasr-sharpen','native','stereo','stereo-480'):
                            candidate=top_image(out/('display-'+name+'.png'))
                            scores=[]
                            for transform in transforms:
                                aligned=candidate if transform is None else candidate.transpose(transform)
                                sample=aligned.crop((110,52,290,89)).point(lambda x:255 if x>135 else 0)
                                scores.append(ImageStat.Stat(ImageChops.difference(reference,sample)).mean[0])
                            if scores.index(min(scores))!=0:raise RuntimeError('Image orientation mismatch: '+name+' '+str(scores))
                        print('PASS captured PICA-SR/native/stereo image orientation against upright SSAA',flush=True)
                        tap('z');tap('g');tap('g');tap(confirm_key());time.sleep(1)
                        subprocess.run(['import','-window','root',str(out/'stereo-settings.png')],env=env,check=True)
                        tap('g');tap('h');saved('stereo_strength 125');tap('f');saved('stereo_strength 100')
                        tap('z')
                        tap('g');tap('g');tap('g');tap('h');saved('low_shadow_detail 1');tap('f');saved('low_shadow_detail 0')
                        tap('g');tap(confirm_key());time.sleep(1)
                        subprocess.run(['import','-window','root',str(out/'bottom-options.png')],env=env,check=True)
                        tap('h');saved('bottom_coins 0');tap('f');saved('bottom_coins 1')
                        tap('g');tap('h');saved('bottom_health 0');tap('f');saved('bottom_health 1')
                        tap('g');tap('h');saved('bottom_monitor 1');time.sleep(1)
                        subprocess.run(['import','-window','root',str(out/'bottom-monitor.png')],env=env,check=True)
                        tap('f');saved('bottom_monitor 0');tap('z')
                    tap('z')
                    tap('g');tap('g');tap('g');tap('g');tap(confirm_key());time.sleep(2)
                    subprocess.run(['import','-window','root',str(out/'controls-submenu.png')],env=env,check=True)
                    tap('h')
                    if 'dialogue_skip_controls 1' not in cfg.read_text():raise RuntimeError('Dialogue option not saved')
                    tap('h')
                    if 'dialogue_skip_controls 0' not in cfg.read_text():raise RuntimeError('Dialogue option not restored')
                    tap('g')
                    if not swap_on:
                        tap('h')
                        swap_on=True
                    subprocess.run(['import','-window','root',str(out/'controls-swap-on.png')],env=env,check=True)
                    tap('h')
                    if 'swap_ba 0' not in cfg.read_text():raise RuntimeError('Swap B/A Off not saved')
                    swap_on=False
                    subprocess.run(['import','-window','root',str(out/'controls-swap-off.png')],env=env,check=True)
                    tap('a')
                    if 'swap_ba 0' not in cfg.read_text():raise RuntimeError('Physical A still confirmed when Swap B/A is Off')
                    tap('s')
                    if 'swap_ba 1' not in cfg.read_text():raise RuntimeError('Physical B confirm did not toggle Swap B/A back On')
                    swap_on=True
                    for (key,exp_default),init_val in zip((('camera_invert_x',1),('camera_invert_y',0),('binoc_invert_x',0),('binoc_invert_y',1)),initial_inverts):
                        tap('g')
                        tap('h')
                        expected_first=0 if init_val else 1
                        if f'{key} {expected_first}' not in cfg.read_text():raise RuntimeError(f'{key} {expected_first} not saved')
                        tap('h')
                        if f'{key} {init_val}' not in cfg.read_text():raise RuntimeError(f'{key} {init_val} not restored')
                        if init_val!=exp_default:tap('h')
                    subprocess.run(['import','-window','root',str(out/'controls-invert-options.png')],env=env,check=True)
                    if args.features:
                        tap('g');tap(confirm_key());time.sleep(2)
                        subprocess.run(['import','-window','root',str(out/'gyro-options.png')],env=env,check=True)
                        tap('h');saved('gyro_enabled 1');tap('f');saved('gyro_enabled 0')
                        tap('g');tap('h');saved('gyro_sensitivity 125');tap('f');saved('gyro_sensitivity 100')
                        tap('g');tap(confirm_key());time.sleep(2)
                        tap('z');tap('z')
                    else:
                        tap('g');tap('g');tap(confirm_key());time.sleep(2)
                    subprocess.run(['import','-window','root',str(out/'options-after-submenus.png')],env=env,check=True)
                tap('z');tap('m');time.sleep(4)
                subprocess.run(['import','-window','root',str(out/'menu-start.png')],env=env,check=True)
                time.sleep(8)
                subprocess.run(['import','-window','root',str(out/'paris-after-menu.png')],env=env,check=True)
                import re as _re
                menu_log=out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR/runtime.log'
                pre_notes=[int(x) for x in _re.findall(r'music notes=(\d+)',menu_log.read_text())] if menu_log.exists() else [0]
                menu_switch_notes=max(pre_notes,default=0)
                tap('m');tap('g');tap(confirm_key());tap('g');tap('g');tap(confirm_key());tap('h');tap('h');tap('z');tap('z');tap('z');time.sleep(5)
                subprocess.run(['import','-window','root',str(out/'audio-restored.png')],env=env,check=True)
                if args.buttons:
                    tap('m');time.sleep(2)
                    tap('t')
                    subprocess.run(['import','-window','root',str(out/'exit-homebrew.png')],env=env,check=True)
                    tap(confirm_key());time.sleep(2)
                    subprocess.run(['import','-window','root',str(out/'exit-confirm.png')],env=env,check=True)
                    tap('f');tap(confirm_key());time.sleep(3)
                else:tap('n+m');time.sleep(2)
            if args.runtime and not args.menu:
                # Enter gameplay through the title and retail in-world camera transition.
                subprocess.run(['xdotool','keydown','m'],env=env,check=True);time.sleep(.2)
                subprocess.run(['xdotool','keyup','m'],env=env,check=True);time.sleep(4)
                subprocess.run(['import','-window','root',str(out/'intro.png')],env=env,check=True)
                time.sleep(4)
                # The first run now plays the genuine opening conversation.
                # Wait for control to return rather than testing during the call.
                import re
                runtime_log=out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR/runtime.log'
                audio_captured=False
                effect_switch_notes=0
                for _ in range(180):
                    contents=runtime_log.read_text() if runtime_log.exists() else ''
                    if 'probe stopped' in contents:raise RuntimeError(contents.splitlines()[-1])
                    dialogs=re.findall(r'binoc=(\d+) dialog=(-?\d+)',contents)
                    seen_dialog=any(value!=('0','-1') for value in dialogs)
                    if seen_dialog and not audio_captured:
                        time.sleep(3)
                        subprocess.run(['import','-window','root',str(out/'dialogue-ui.png')],env=env,check=True)
                        effect_switch_notes=max([int(x) for x in re.findall(r'music notes=(\d+)',contents)],default=0)
                        audio_captured=True
                    if seen_dialog and len(dialogs)>=3 and all(value==('0','-1') for value in dialogs[-3:]):break
                    subprocess.run(['xdotool','keydown','a'],env=env,check=True);time.sleep(.1)
                    subprocess.run(['xdotool','keyup','a'],env=env,check=True);time.sleep(.9)
                else:raise RuntimeError('Opening dialog did not return control within 180 seconds')
                for _ in range(2):
                    subprocess.run(['xdotool','keydown','a'],env=env,check=True);time.sleep(.1)
                    subprocess.run(['xdotool','keyup','a'],env=env,check=True);time.sleep(.9)
                time.sleep(2)
                subprocess.run(['xdotool','keydown','a'],env=env,check=True);time.sleep(.3)
                subprocess.run(['import','-window','root',str(out/'jumping.png')],env=env,check=True)
                subprocess.run(['xdotool','keyup','a'],env=env,check=True);time.sleep(3)
                subprocess.run(['import','-window','root',str(out/'landed.png')],env=env,check=True)
                if args.effects:
                    subprocess.run(['xdotool','keydown','x'],env=env,check=True);time.sleep(.2)
                    subprocess.run(['xdotool','keyup','x'],env=env,check=True);time.sleep(.15)
                    subprocess.run(['import','-window','root',str(out/'cane-effects.png')],env=env,check=True)
                    time.sleep(3)
                    subprocess.run(['xdotool','keydown','w'],env=env,check=True);time.sleep(.2)
                    subprocess.run(['xdotool','keyup','w'],env=env,check=True);time.sleep(4)
                    subprocess.run(['import','-window','root',str(out/'binocular-ui.png')],env=env,check=True)
                    subprocess.run(['xdotool','keydown','w'],env=env,check=True);time.sleep(.2)
                    subprocess.run(['xdotool','keyup','w'],env=env,check=True);time.sleep(4)
            if args.menu:
                contents=(out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR/runtime.log').read_text()
                audio=(out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR/runtime-stderr.log').read_text()
                for marker in ('Original title menu:', 'Loading original Splash world', 'Loading original Splash / Paris transition', 'Native runtime probe exited'):
                    if marker not in contents:raise RuntimeError('Missing menu check: '+marker)
                if 'probe stopped' in contents:raise RuntimeError(contents.splitlines()[-1])
                if args.features:
                    for marker in ('requested=1 actual=0','requested=2 actual=2','requested=6 actual=6 (400x480, eyes=2','Stereo eyes: 2 slider=1.00','Texture memory:','Bottom HUD target: ready'):
                        if marker not in audio:raise RuntimeError('Missing display/memory check: '+marker)
                    for line in ('display_mode 0','stereo_strength 100','gyro_enabled 0','gyro_sensitivity 100','low_shadow_detail 0'):
                        if line not in cfg.read_text().splitlines():raise RuntimeError('Feature default not restored: '+line)
                    print('PASS display switches, two eye passes, shadow and gyro settings, nonblocking calibration',flush=True)
                audio_markers=[] if args.skip_intro else ['Movie 17 ended valid=1']
                if args.skip_intro and 'Skipping previously viewed startup video' not in contents:raise RuntimeError('Saved intro skip was not used')
                if not args.buttons:audio_markers.append('NDSP unavailable; using OpenDSP')
                for marker in audio_markers:
                    if marker not in audio:raise RuntimeError('Missing audio/movie check: '+marker)
                import re
                music=[tuple(map(int,row)) for row in re.findall(r'music notes=(\d+) active=(\d+) sample_bytes=(\d+) dropped=(\d+)',contents)]
                if not music or max(row[0] for row in music)<10 or not any(row[1] and row[2] for row in music):raise RuntimeError('Music did not produce active DSP voices')
                if music[-1][2]==0 or music[-1][0]<=menu_switch_notes:raise RuntimeError(f'Music stopped after backend switch: before={menu_switch_notes}, last={music[-1]}')
                audio_failures=[int(x) for x in re.findall(r'audio voices=\d+ failures=(\d+)',contents)]
                if not audio_failures or any(f!=0 for f in audio_failures):raise RuntimeError(f'Audio failures reported in log: {audio_failures}')
                if args.buttons:
                    for marker in ('Button icons: '+initial_icons+' (loaded)', 'Swap B/A: '+initial_swap+' (loaded)', 'Button icons: PS2 (saved=1)', 'Button icons: New 3DS (saved=1)', 'Dialogue skip controls: 1 (saved=1)', 'Dialogue skip controls: 0 (saved=1)', 'Swap B/A: On (saved=1)', 'Swap B/A: Off (saved=1)', 'Camera Invert X: On (saved=1)', 'Camera Invert X: Off (saved=1)', 'Camera Invert Y: On (saved=1)', 'Camera Invert Y: Off (saved=1)', 'Binoc Invert X: On (saved=1)', 'Binoc Invert X: Off (saved=1)', 'Binoc Invert Y: On (saved=1)', 'Binoc Invert Y: Off (saved=1)', 'Native text atlas: 512x512 LA8, full 8-bit glyph coverage'):
                        if marker not in audio:raise RuntimeError('Missing button check: '+marker)
                    print('PASS loaded '+initial_icons+' icons, live PS2/New 3DS switching, Swap B/A + invert toggles, saved choice and menu exit',flush=True)
                if args.buttons:print('PASS original menus, music DSP voices preserved across switch, intro and clean exit',flush=True)
                else:print('PASS original Sound/title menus, music DSP voices preserved across switch, intro, NDSP fallback and clean exit',flush=True)
                raise SystemExit(0)
            if args.effects:
                import re
                if args.stereo_bottom:
                    # Enabled bottom counters must survive the retail auto-hide
                    # timer without pausing or collecting anything.
                    time.sleep(5)
                    subprocess.run(['import','-window','root',str(out/'bottom-counters.png')],env=env,check=True)
                effects=(out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR/runtime-stderr.log').read_text()
                contents=(out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR/runtime.log').read_text()
                if args.stereo_bottom:
                    if 'requested=6 actual=6 (400x480, eyes=2' not in effects:raise RuntimeError('400x480 stereo did not remain active')
                    if 'Stereo eyes: 2 slider=1.00' not in effects:raise RuntimeError('Stereo gameplay did not draw both eyes')
                    if not re.search(r'Bottom HUD: active=1 coins=\d+ health=\d+ dialogue=[1-9]\d* portraits=[1-9]',effects):raise RuntimeError('Portrait dialogue was not routed to the bottom')
                    if not re.search(r'Bottom HUD: active=1 coins=[1-9]\d* health=[1-9]',effects):raise RuntimeError('Coins and health were not routed to the bottom')
                    print('PASS 400x480 stereo gameplay, bottom counters and animated portrait dialogue',flush=True)
                music=[tuple(map(int,row)) for row in re.findall(r'music notes=(\d+) active=(\d+) sample_bytes=(\d+) dropped=(\d+)',contents)]
                if not music or music[-1][2]==0 or music[-1][0]<=effect_switch_notes:raise RuntimeError(f'Music stopped after mid-dialogue switch: before={effect_switch_notes}, last={music[-1] if music else None}')
                audio_failures=[int(x) for x in re.findall(r'audio voices=\d+ failures=(\d+)',contents)]
                if not audio_failures or any(f!=0 for f in audio_failures):raise RuntimeError(f'Audio failures reported in effects log: {audio_failures}')
                samples=[tuple(map(int,row)) for row in re.findall(r'Native effects: particles=(\d+) shadow-maps=(\d+) receivers=(\d+) shadow-volumes=(\d+)',effects)]
                if not samples or not any(row[0]>0 for row in samples):raise RuntimeError('No original particles rendered')
                if not any(row[1]>0 and row[2]>0 for row in samples):raise RuntimeError('No character shadow receivers rendered')
                cel=[tuple(map(int,row)) for row in re.findall(r'Native cel: packets=(\d+) segments=(\d+) lights=(\d+) materials=(\d+)',effects)]
                if not cel or not any(row[1]>0 and row[2]>0 and row[3]>0 for row in cel):raise RuntimeError('No native silhouette edges or lit three-way materials rendered')
                if 'Controller icon atlases ready: PS2 + New 3DS (256x128)' not in effects:raise RuntimeError('Controller atlases were not prepared for the binocular HUD')
                print('PASS native particles, animated shadows, lights, three-way materials and silhouette edges',flush=True)
            subprocess.run(['xdotool','keydown','Up'],env=env,check=True);time.sleep(2)
            subprocess.run(['xdotool','keyup','Up'],env=env,check=True);time.sleep(1)
            subprocess.run(['import','-window','root',str(out/'moved.png')],env=env,check=True)
            subprocess.run(['xdotool','keydown','n+m' if args.runtime else 'm'],env=env,check=True);time.sleep(.2)
            subprocess.run(['xdotool','keyup','n+m' if args.runtime else 'm'],env=env,check=True);time.sleep(3)
            subprocess.run(['import','-window','root',str(out/'after-start.png')],env=env,check=True)
    finally:
        if emulator is not None and emulator.poll() is None:
            emulator.terminate()
            try:emulator.wait(timeout=5)
            except subprocess.TimeoutExpired:emulator.kill();emulator.wait()
        if x.poll() is None:x.terminate();x.wait(timeout=5)

if args.runtime and not args.menu:
    subprocess.run([sys.executable,str(ROOT/'platform/3ds/check_runtime_log.py'),str(out/'data/azaharplus-emu/sdmc/3ds/ThieviusCTR/runtime.log')],check=True)
