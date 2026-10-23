"""Order 6 live GPU/API validation (requires Pillow and an empty test document).
Run from the repository root; writes PNGs, a native scene and JSON under build/.
Uses local CAD controls and GPU readback, without native pointer interaction.
"""
import sys,time,json,statistics
from pathlib import Path
from PIL import Image
sys.path.insert(0,'tools/cad_api')
from client import Client
out=Path('build/order6-live').resolve()
checks=[]; measurements={}
def check(ok,name):
    checks.append({'name':name,'ok':bool(ok)})
    if not ok: raise AssertionError(name)
def state(): return c.ui()['state']
def capture(name):
    p=out/(name+'.png')
    # Unique filenames or an explicitly owned previous test artifact only.
    p.unlink(missing_ok=True)
    c.ui('camera',operation='capture',path=str(p))
    for _ in range(100):
        if p.exists():
            try:
                im=Image.open(p).convert('RGB');im.load();return im
            except OSError: pass
        time.sleep(.05)
    raise RuntimeError('Capture not written: '+str(p))
def fraction(im,predicate,box=None):
    pixels=(im.crop(box) if box else im).getdata()
    return sum(predicate(*p) for p in pixels)/len(pixels)
def grid(im):
    w,h=im.size
    return all(fraction(im,lambda r,g,b:g>20 and b>25,(int(w*x),int(h*.6),int(w*(x+.3)),int(h*.95)))>.05 for x in (0,.7))
def set_camera(**kw):
    c.ui('camera',operation='set',**kw);time.sleep(.12)
try:
    for _ in range(100):
        try:
            c=Client()
            if c.request('/v1/status')[0]==200: break
        except (OSError,ValueError): pass
        time.sleep(.2)
    c.call('open_cad_workspace')
    s=state()
    if s['features'] or s['modified']: raise RuntimeError('Requires an empty, unmodified test document')
    for _ in range(60):
        s=state()
        if s['camera']['floorReady']: break
        time.sleep(.1)
    check(s['camera']['floorReady'] and 'OpenGL 4.1' in s['renderStatus'],'Real OpenGL context and floor shader initialized')
    baseline=s['documentRevision']; uploads=s['camera']['geometryUploads']
    for distance in (.0001,200,10000,5000000):
        set_camera(distance=distance,yawDeg=45,pitchDeg=35,panX=0,panY=0)
        im=capture('grid-'+str(distance));check(grid(im),'Floor reaches both lower corners at distance '+str(distance)+' mm')
    set_camera(distance=10000,yawDeg=80,pitchDeg=5,panX=2000000,panY=-100000)
    im=capture('horizon-pan');w,h=im.size
    check(grid(im),'Floor follows large pan and orbit without a finite patch edge')
    check(fraction(im,lambda r,g,b:r<12 and g<17 and b<22,(0,0,w,int(h*.2)))>.98,'Horizon ray discard matches current five-degree camera')
    check(state()['documentRevision']==baseline,'Camera and captures leave document revision unchanged')
    c.call('cad_workspace',{'action':'guides','workspace':'cad','guides':{'floor':False}})
    im=capture('floor-off');check(fraction(im,lambda r,g,b:r<12 and g<17 and b<22)>.99,'Existing floor toggle removes the shader pass')
    c.call('cad_workspace',{'action':'guides','workspace':'cad','guides':{'floor':True}})
    check(state()['camera']['gridVertices']==3,'Floor geometry stays at three vertices')
    bad=c.call('cad_ui',{'action':'camera','operation':'set','distance':-1},allow_failure=True)
    check(not bad['ok'] and state()['camera']['distance']==10000,'Invalid camera distance fails without changing the camera')
    tiny=c.call('cad_create_box',{'width':.01,'depth':.01,'height':.01})['id']
    c.ui('camera',operation='fit');time.sleep(.2)
    check(state()['camera']['distance']<.1,'Zoom to Fit frames a 0.01 mm solid')
    c.ui('select',id=tiny);c.ui('camera',operation='fitSelection');time.sleep(.15)
    im=capture('tiny-selected');check(fraction(im,lambda r,g,b:r>40 and r>g*1.4 and b>g*1.4)>.05,'Small object and selection highlight survive precision zoom')
    large=c.call('cad_create_box',{'width':10000,'depth':10000,'height':10000})['id']
    c.call('cad_set_placement',{'id':large,'x':0,'y':1000000,'z':0})
    c.ui('camera',operation='fit');time.sleep(.15)
    s=state();check(s['camera']['distance']>1000000 and s['camera']['nearPlane']>1000,'Large separated assembly receives a tight fitted clipping range')
    im=capture('assembly-fit');check(fraction(im,lambda r,g,b:b>45 and g>r*1.2)>.00001,'Large remote assembly geometry remains rendered')
    c.ui('select',id=tiny);c.ui('camera',operation='fitSelection');time.sleep(.15)
    s=state();check(s['camera']['distance']<.1 and s['camera']['depthMode']=='logarithmic','Selection framing enters logarithmic depth for a tiny part in a remote assembly')
    im=capture('tiny-wide-depth');check(fraction(im,lambda r,g,b:r>40 and r>g*1.4 and b>g*1.4)>.05,'Logarithmic depth renders the tiny selected body without disappearance')
    near,far=s['camera']['nearPlane'],s['camera']['farPlane'];measurements['wideDepthRatio']=far/near
    revision=s['documentRevision'];previousUploads=s['camera']['geometryUploads'];timings=[]
    for i in range(24):
        set_camera(yawDeg=45+i*.2)
        timings.append(state()['camera']['floorGpuMs'])
    s=state();check(s['camera']['geometryUploads']==previousUploads,'Orbit does not upload or regenerate model geometry')
    check(s['documentRevision']==revision,'Orbit and selection framing preserve engineering history')
    timings=timings[4:];measurements.update(floorGpuMedianMs=statistics.median(timings),floorGpuMaxMs=max(timings),captureSize=list(im.size),renderStatus=s['renderStatus'])
    # Save the owned validation scene before closing it later.
    path=out/'viewport-range.djo';c.ui('document',operation='save',path=str(path),overwrite=True)
    c.ui('document',operation='new');c.ui('document',operation='open',path=str(path));c.ui('camera',operation='fit');time.sleep(.15)
    s=state();check(len(s['features'])==2 and not s['modified'],'Native reopen retains both test bodies with fitted camera visibility')
    im=capture('reopened');check(fraction(im,lambda r,g,b:b>45 and g>r*1.2)>.00001,'Reopened assembly renders after context and bounds update')
    c.ui('camera',operation='reset');time.sleep(.1);s=state();check(abs(s['camera']['yawDeg']-45)<.001 and s['camera']['panX']==0,'Reset preserves the established isometric convention')
    check(not s['modified'],'Test document is saved and unmodified at closeout')
except Exception as error:
    checks.append({'name':str(error),'ok':False})
    raise
finally:
    result={'ok':all(x['ok'] for x in checks),'passed':sum(x['ok'] for x in checks),'failed':sum(not x['ok'] for x in checks),'checks':checks,'measurements':measurements}
    (out/'result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))
