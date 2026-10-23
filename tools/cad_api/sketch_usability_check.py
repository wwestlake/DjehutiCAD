"""Sketch usability smoke check in an EMPTY running CAD document.
Uses the application's local API and component renders, without native input.
Writes owned test artifacts under build/sketch-usability-live/ and leaves its scene saved.
Run with --reopen after restarting the app to verify toolbox preference persistence.
"""
import json,sys,time
from pathlib import Path
from client import Client
out=Path('build/sketch-usability-live').resolve();out.mkdir(parents=True,exist_ok=True)
checks=[]
def check(ok,label):
    checks.append({'name':label,'ok':bool(ok)})
    if not ok: raise AssertionError(label)
for _ in range(100):
    try:
        c=Client()
        if c.request('/v1/status')[0]==200: break
    except (OSError,ValueError): pass
    time.sleep(.2)
else: raise RuntimeError('API unavailable')
def state(): return c.ui()['state']
def capture(name): return c.ui('sketchToolbox',operation='capture',path=str(out/(name+'.png')),overwrite=True)
def draw(radius):
    c.ui('command',command='circle');c.ui('sketch',phase='begin',x=0,y=0)
    return c.ui('sketch',phase='commit',x=radius,y=0)['state']
try:
    c.call('open_cad_workspace');s=state()
    if '--reopen' in sys.argv:
        check(s['sketchToolbox']['visible'] and not s['sketchToolbox']['docked'] and s['sketchToolbox']['x']==120 and s['sketchToolbox']['y']==45,'Toolbox position/docking/visibility persisted through actual application restart')
        check(not s['modified'],'Restarted document is not modified')
        c.ui('document',operation='open',path=str(out/'circles.djo'))
        s=state();sketch=next(f for f in s['features'] if f['type']=='sketch');ids=[e['id'] for e in sketch['entities']]
        c.ui('select',id=sketch['id'],entity=ids[0]);s=state()
        check(len(ids)==2 and s['sketchToolbox']['showing'],'Native reopen restores both circle rows and the floating toolbox')
        capture('restarted')
    else:
        if s['features'] or s['modified']: raise RuntimeError('Use an empty, unmodified test document')
        c.ui('command',command='sketch');c.ui('click',button='planeXY');s=c.ui('click',button='apply')['state'];sketch=s['editingSketch']
        check(s['mode']=='sketch' and s['sketchToolbox']['showing'],'New XY sketch automatically shows toolbox')
        s=draw(50);first=s['selectedEntity']
        check(first and s['task'].startswith('Circle') and any(f['id']=='dimension' and float(f['value'])==50 for f in s['fields']),'50 mm circle immediately exposes selected radius properties')
        second=draw(30)['selectedEntity'];third=draw(10)['selectedEntity'];s=state();entities=s['features'][0]['entities']
        check(len(entities)==3 and all(e['treeName'].startswith('Circle') for e in entities),'All three concentric circles appear as named entity rows')
        c.ui('select',id=sketch,entity=first);s=state()
        check(s['selectedEntity']==first and s['gesture']['tool']=='select' and next(e for e in s['features'][0]['entities'] if e['id']==first)['treeSelected'],'Tree selection opens shared properties and Select tool')
        c.ui('set',field='dimension',value=55);s=c.ui('click',button='apply')['state']
        check(next(e for e in s['features'][0]['entities'] if e['id']==first)['radius']==55,'Properties Apply updates circle radius to 55 mm')
        c.ui('select',entity=second,second=True);s=state()
        check(s['secondEntity']==second and sum(e['treeSelected'] for e in s['features'][0]['entities'])==2,'Existing two-entity selection is mirrored in the tree')
        c.ui('select',id=sketch,entity=third);s=c.ui('click',button='deleteSketchEntities')['state']
        check(len(s['features'][0]['entities'])==2 and not s['selectedEntity'] and s['gesture']['tool']=='select','Properties Delete removes selected geometry and clears its inspector')
        s=c.ui('command',command='undo')['state'];check(len(s['features'][0]['entities'])==3 and any(e['id']==third for e in s['features'][0]['entities']),'Undo restores the deleted circle and stable identifier')
        s=c.ui('command',command='redo')['state'];check(len(s['features'][0]['entities'])==2,'Redo removes the same circle again')
        before=s['documentRevision'];c.ui('command',command='circle');c.ui('sketch',phase='begin',x=0,y=0)
        bad=c.call('cad_ui',{'action':'sketch','phase':'commit','x':0,'y':0},allow_failure=True)
        check(not bad['ok'] and bad['state']['documentRevision']==before and len(bad['state']['features'][0]['entities'])==2,'Zero-radius gesture fails without a new entity/history edit')
        for radius in (1e-9,-1):
            bad=c.call('cad_sketch_circle',{'sketch':sketch,'x':0,'y':0,'radius':radius},allow_failure=True)
            check(not bad['ok'],'Model command rejects invalid radius '+str(radius))
        c.ui('command',command='sketchSelect');c.ui('select',id=sketch,entity=first)
        before=state()['documentRevision'];c.ui('sketchToolbox',value={'visible':True,'docked':False,'x':120,'y':45});s=state()
        check(s['sketchToolbox']['x']==120 and not s['sketchToolbox']['docked'],'Toolbox floats at a controlled position over the sketch')
        capture('floating')
        c.ui('command',command='dockSketchToolbox');check(state()['sketchToolbox']['docked'],'View docking command docks the toolbox')
        capture('docked')
        c.ui('command',command='sketchToolbox');check(not state()['sketchToolbox']['showing'],'View visibility command hides the toolbox')
        c.ui('command',command='sketchToolbox');c.ui('command',command='dockSketchToolbox');s=state()
        check(s['documentRevision']==before,'Toolbox presentation changes leave engineering history unchanged')
        c.ui('document',operation='save',path=str(out/'circles.djo'),overwrite=True)
        c.ui('document',operation='new');c.ui('document',operation='open',path=str(out/'circles.djo'));s=state()
        check(len(s['features'][0]['entities'])==2 and not s['modified'],'Native save/reopen preserves the actual remaining geometry')
        c.ui('select',id=sketch,entity=first);capture('reopened')
    check(not state()['modified'],'Owned test document is saved and unmodified at closeout')
except Exception as error:
    checks.append({'name':str(error),'ok':False});raise
finally:
    r={'ok':all(x['ok'] for x in checks),'passed':sum(x['ok'] for x in checks),'failed':sum(not x['ok'] for x in checks),'checks':checks}
    (out/('restart-result.json' if '--reopen' in sys.argv else 'result.json')).write_text(json.dumps(r,indent=2))
    print(json.dumps(r,indent=2))
