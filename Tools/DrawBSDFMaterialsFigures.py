"""Original editable SVG and 1920x1080 PNG teaching diagrams. MIT, masafumi 2026."""
from pathlib import Path
from html import escape
import math
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'Docs/Assets/Images/BSDFMaterials'
BG='#f4f7fb';INK='#172d48';MUTED='#52677e';BLUE='#2573c8';RED='#c65040';GREEN='#168974'
class Figure:
    def __init__(self,title,subtitle):
        self.im=Image.new('RGB',(1920,1080),BG);self.d=ImageDraw.Draw(self.im)
        self.svg=['<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="1080" viewBox="0 0 1920 1080">',f'<rect width="1920" height="1080" fill="{BG}"/>']
        self.text(64,38,title,46);self.text(64,112,subtitle,28,MUTED)
    def text(self,x,y,value,size=30,color=INK):
        font=ImageFont.truetype('C:/Windows/Fonts/YuGothM.ttc',size)
        for i,line in enumerate(value.split('\n')):
            yy=y+i*(size+14);self.d.text((x,yy),line,font=font,fill=color)
            self.svg.append(f'<text x="{x}" y="{yy+size}" font-family="Yu Gothic, sans-serif" font-size="{size}" fill="{color}">{escape(line)}</text>')
    def rect(self,x,y,w,h,fill='white',stroke='#d2dce8'):
        self.d.rounded_rectangle((x,y,x+w,y+h),radius=18,fill=fill,outline=stroke,width=2)
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="18" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
    def line(self,points,color=BLUE,width=5,arrow=False):
        self.d.line(points,fill=color,width=width,joint='curve');self.svg.append('<polyline points="'+' '.join(f'{x},{y}' for x,y in points)+f'" fill="none" stroke="{color}" stroke-width="{width}"/>')
        if arrow:
            (a,b),(x,y)=points[-2:];angle=math.atan2(y-b,x-a);p=[(x,y),(x-23*math.cos(angle-.45),y-23*math.sin(angle-.45)),(x-23*math.cos(angle+.45),y-23*math.sin(angle+.45))]
            self.d.polygon(p,fill=color);self.svg.append('<polygon points="'+' '.join(f'{x},{y}' for x,y in p)+f'" fill="{color}"/>')
    def circle(self,x,y,r,fill='white',stroke=BLUE):
        self.d.ellipse((x-r,y-r,x+r,y+r),fill=fill,outline=stroke,width=4);self.svg.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}" stroke="{stroke}" stroke-width="4"/>')
    def save(self,name):
        OUT.mkdir(parents=True,exist_ok=True);self.im.save(OUT/(name+'.png'));(OUT/(name+'.svg')).write_text('\n'.join(self.svg+['</svg>'])+'\n',encoding='utf-8',newline='\n')
def hemispheres():
    f=Figure('BRDF・BTDF・BSDF：表面のどちら側へ進むか','BRDFは反射、BTDFは透過。BSDFはこの二つをまとめた表面散乱の表現です。')
    for i,(title,reflection,transmission) in enumerate([('BRDF：同じ側へ反射',True,False),('BTDF：反対側へ透過',False,True),('BSDF：反射と透過',True,True)]):
        x=60+i*620;cx=x+290;f.rect(x,220,590,690);f.text(x+26,248,title,31)
        f.rect(x+22,580,546,260,'#e4f4ed');f.line([(x+22,580),(x+568,580)],MUTED,3)
        f.line([(cx,350),(cx,810)],'#a5b1c0',2);f.text(cx+14,360,'法線',22,MUTED)
        f.line([(cx-175,385),(cx,580)],BLUE,6,True);f.text(x+35,420,'入射光',24,BLUE)
        if reflection:f.line([(cx,580),(cx+160,380)],RED,6,True);f.text(cx+60,450,'反射',26,RED)
        if transmission:f.line([(cx,580),(cx+95,790)],GREEN,6,True);f.text(cx+112,720,'透過',26,GREEN)
    f.text(70,955,'矢印は光の進行方向。数式の wi / wo は、どちらも表面から外向きに定義します。',29)
    f.save('hemispheres')
def fresnel():
    f=Figure('Fresnel・屈折・全反射','屈折率は反射量と曲がり方の両方を決めます。ここでは滑らかな誘電体の境界を描きます。')
    f.rect(60,220,940,760);f.rect(80,560,900,380,'#e4f4ed');f.line([(100,560),(960,560)],MUTED,3)
    f.text(100,270,'空気  η = 1.0',32);f.text(100,600,'ガラス  η = 1.5',32)
    f.line([(650,340),(650,930)],'#9aa9b8',2)
    f.line([(476.795,260),(650,560)],BLUE,6,True);f.line([(650,560),(823.205,260)],RED,6,True);f.line([(650,560),(773.744,910)],GREEN,6,True)
    f.text(345,420,'入射 30°',29,BLUE);f.text(720,680,'屈折 約19.47°',27,GREEN)
    f.text(100,850,'垂直入射の反射率 F₀ = 0.04\n反射と透過の選択確率は F と 1 − F',25)
    f.rect(1040,220,820,760);x0,y0,w,h=1150,840,620,460
    f.text(1090,255,'角度によるFresnel反射率',33)
    f.line([(x0,y0-h),(x0,y0),(x0+w,y0)],MUTED,3)
    for eta,color,name in [(1.5,BLUE,'空気 → ガラス'),(1/1.5,GREEN,'ガラス → 空気')]:
        points=[]
        for i in range(361):
            theta=i/360*math.pi/2;c=math.cos(theta);s2=(1-c*c)/(eta*eta)
            if s2>=1:F=1
            else:
                ct=math.sqrt(max(0,1-s2));F=.5*((eta*c-ct)/(eta*c+ct))**2+.5*((c-eta*ct)/(c+eta*ct))**2
            points.append((x0+w*i/360,y0-h*F))
        f.line(points,color,5);f.text(1450,305 if eta>1 else 350,name,27,color)
    critical=math.degrees(math.asin(1/1.5));xc=x0+w*critical/90
    f.line([(xc,y0),(xc,y0-h)],GREEN,2);f.text(xc-90,860,'約41.8°',26,GREEN)
    f.text(x0-45,y0-15,'0',24,MUTED);f.text(x0-45,y0-h-15,'1',24,MUTED);f.text(x0+w-30,y0+22,'90°',24,MUTED)
    f.text(1090,930,'ガラス → 空気：臨界角以上で全反射',26)
    f.save('fresnel-refraction')
def absorption():
    f=Figure('厚みと吸収：表面のBSDFと内部の減衰を分ける','同じ表面・同じ吸収係数でも、内部を長く進むほど透過光が減ります。')
    color=(.18,.65,.85);distance=.5
    for i,(d,label) in enumerate([(.1,'薄い板：0.1 m'),(.5,'厚い板：0.5 m')]):
        x=70+i*930;f.rect(x,220,880,510);f.text(x+35,250,label,36)
        bx=x+245;bw=130 if i==0 else 360;f.rect(bx,350,bw,250,'#b6dedb',GREEN)
        f.line([(x+35,475),(bx,475)],BLUE,7,True)
        out=tuple(c**(d/distance) for c in color);hexcolor='#'+''.join(f'{round(c*255):02x}' for c in out)
        f.line([(bx+bw,475),(x+830,475)],hexcolor,15,True)
        f.text(x+35,635,'内部の透過率 T = ('+', '.join(f'{c:.3f}' for c in out)+')',27)
    f.rect(70,780,1780,230,'white');f.text(105,810,'sigmaA = −ln(C) / D     T(d) = exp(−sigmaA d) = C^(d / D)',34)
    f.text(105,880,'C = (0.18, 0.65, 0.85)、D = 0.5 m。図の数値は内部だけの減衰です。',29)
    f.text(105,935,'境界ではFresnel反射・屈折を評価し、その間の実距離へ吸収を適用します。',29)
    f.save('absorption-thickness')
def modes():
    f=Figure('同じアセットを3モードで評価する','形状・カメラ・照明・露出は共通。ガラスに指定したマテリアルの評価だけを切り替えます。')
    rows=[('BSDF：反射＋透過','屈折した先の像が見える\n内部の実距離で色が変わる',True),('BRDF：反射成分のみ','同じFresnel反射を残す\n透過した先の寄与を省く',False),('従来の不透明PBR','Lambert＋GGXで不透明評価\n透過情報を使わない再現モード',False)]
    for i,(name,desc,trans) in enumerate(rows):
        x=60+i*620;cx=x+285;f.rect(x,220,590,760);f.text(x+24,250,name,30)
        f.circle(cx,530,120,'#d8eeee' if i==0 else '#e3e8ef' if i==2 else '#37404b')
        f.line([(x+55,390),(cx-70,440)],BLUE,6,True)
        f.line([(cx-70,440),(cx-210,350)],RED,6,True)
        if trans:f.line([(cx-70,440),(cx+75,580),(cx+230,675)],GREEN,6,True)
        if i==2:
            for end in [(cx-160,550),(cx-190,480),(cx-130,620)]:f.line([(cx-95,520),end],RED,3,True)
        f.text(x+28,760,desc,28)
    f.text(70,1010,'反射のみのモードでは、取り除いた透過のエネルギーを反射へ振り替えません。',28)
    f.save('three-modes')
if __name__=='__main__':
    hemispheres();fresnel();absorption();modes();print('4 original SVG / 1920x1080 PNG pairs written')
