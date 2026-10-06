"""Preserve supplied cockpit geometry, replace unverified artwork with owned finishes."""
assert not assembly.get('su57_cockpit_corrected'),'Cockpit already corrected'
charcoal=material('cockpit charcoal',(.025,.030,.035,1),.12,.58)
cushion=material('seat woven olive fabric',(.040,.054,.032,1),0,.88)
for obj in aircraft.objects:
 if obj.type!='MESH':continue
 for i,mat in enumerate(obj.data.materials):
  if mat and mat.name=='Black':obj.data.materials[i]=cushion if 'Eject_seat' in obj.name else charcoal
# Retained source seat shape; a separate HUD and display bezels provide readable depth.
bezel=material('display bezels',(.012,.015,.019,1),.1,.5)
screen=material('powered displays',(.008,.020,.016,1),0,.30)
p=next(n for n in screen.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
p.inputs['Emission Color'].default_value=(.015,.12,.07,1);p.inputs['Emission Strength'].default_value=.4
mark=material('display phosphor',(.18,.50,.30,1),0,.45)
p=next(n for n in mark.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
p.inputs['Emission Color'].default_value=(.18,.65,.35,1);p.inputs['Emission Strength'].default_value=.6
for tag,y in [('left',-.185),('right',.185)]:
 box(tag+' multi-function display frame',(3.70,y,3.01),(.044,.31,.30),bezel,lod=1)
 box(tag+' multi-function display screen',(3.731,y,3.01),(.008,.262,.247),screen,lod=1)
 for row in range(5):
  box(tag+' display horizontal tick '+str(row),(3.738,y,2.94+row*.032),(.002,.12 if row==2 else .06,.003),mark,lod=0)
 for col in range(4):
  box(tag+' bezel button '+str(col),(3.738,y-.09+col*.06,2.863),(.007,.020,.012),charcoal,lod=0)
 # A compass/map cross gives authored detail without claiming an operational display.
 box(tag+' display vertical reference',(3.739,y,3.028),(.002,.003,.14),mark,lod=0)
glass=bpy.data.materials['Su57 | canopy glass'].copy();glass.name='Su57 | HUD combiner glass'
p=next(n for n in glass.node_tree.nodes if n.type=='BSDF_PRINCIPLED');p.inputs['Alpha'].default_value=.08
obj=mesh('HUD combiner',[(3.49,-.15,3.18),(3.49,.15,3.18),(3.55,.15,3.40),(3.55,-.15,3.40)],[(0,1,2,3)],glass,smooth=False,lod=1)
for sign in [-1,1]:rod('HUD support '+str(sign),(3.46,sign*.17,3.07),(3.51,sign*.17,3.23),.012,charcoal,lod=1,sides=10)
assembly['su57_cockpit_corrected']=True
assembly['cockpit']='ESTIMATE: retained supplied geometry, owned charcoal/fabric/screens/HUD; not a working avionics replica'
scene.view_settings.exposure=0
# Dynamic OCIO view names are queried from the validated config, rather than guessed.
import PyOpenColorIO as ocio
views=list(ocio.Config.CreateFromFile(str(ROOT/'.cache/blender_ocio/config.ocio')).getViews(scene.display_settings.display_device))
view=next((v for v in views if v.lower()=='agx'),scene.view_settings.view_transform)
try:scene.view_settings.view_transform=view
except TypeError as e:print('View transform remains',scene.view_settings.view_transform,e)
remember_rest()
bpy.ops.wm.save_as_mainfile(filepath=str(WORKING_PATH))
print('Owned cockpit finishes, displays and HUD saved; view',scene.view_settings.view_transform)
