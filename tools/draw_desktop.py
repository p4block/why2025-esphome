"""Render the badge's geometric desktop and shape-key dock at panel resolution."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
im=Image.new('RGB',(720,720),'#080e14');d=ImageDraw.Draw(im)
font=ImageFont.load_default(size=14)
large=ImageFont.load_default(size=40)
small=ImageFont.load_default(size=11)
cyan='#63e8ef';muted='#567884';edge='#243f4b'
d.text((24,22),'WHY2025 / FIELD SYSTEM',font=font,fill=cyan)
d.line((24,58,696,58),fill=edge,width=1)
d.text((24,65),'P4     //     TELEMETRY INTERFACE',font=small,fill=muted)
# A restrained, asymmetrical racing-interface motif.
d.polygon([(100,230),(398,230),(358,270),(100,270)],outline=edge,width=2)
d.polygon([(410,230),(470,230),(430,270),(370,270)],fill=cyan)
d.line((100,290,610,290),fill=edge,width=1)
d.text((100,318),'WHY / 25',font=large,fill='#d1e6e9')
d.text((102,380),'CONNECTED HARDWARE. LOCAL CONTROL.',font=small,fill=muted)
for i in range(7):
 x=102+i*25;d.line((x,442,x+15,427),fill=muted,width=2)
d.text((102,486),'SELECT A MODULE BELOW',font=font,fill=cyan)
d.line((24,622,696,622),fill=edge)
labels=['CONSOLE','SENSORS','DEVICE','LIGHTS','LINK','GUIDE']
for i,name in enumerate(labels):
 x=14+i*116;cx=x+56;cy=658
 if i==0:d.rectangle((cx-8,cy-8,cx+8,cy+8),outline=cyan,width=2)
 elif i==1:d.polygon([(cx,cy-9),(cx-10,cy+8),(cx+10,cy+8)],outline=cyan,width=2)
 elif i==2:
  d.line((cx-7,cy-7,cx+7,cy+7),fill=cyan,width=2);d.line((cx-7,cy+7,cx+7,cy-7),fill=cyan,width=2)
 elif i==3:d.ellipse((cx-9,cy-9,cx+9,cy+9),outline=cyan,width=2)
 elif i==4:
  d.arc((cx-12,cy-3,cx+1,cy+9),80,270,fill=cyan,width=2)
  d.arc((cx-5,cy-10,cx+9,cy+5),180,350,fill=cyan,width=2)
  d.arc((cx+3,cy-4,cx+14,cy+9),270,450,fill=cyan,width=2)
  d.line((cx-7,cy+9,cx+8,cy+9),fill=cyan,width=2)
 else:d.polygon([(cx,cy-10),(cx+10,cy),(cx,cy+10),(cx-10,cy)],outline=cyan,width=2)
 box=d.textbbox((0,0),name,font=small);w=box[2]-box[0]
 d.text((cx-w//2,682),name,font=small,fill='#d1e6e9')
(ROOT/'assets').mkdir(exist_ok=True)
im.save(ROOT/'assets/badge-desktop.png')
im.save(ROOT/'assets/badge-preview.png')
