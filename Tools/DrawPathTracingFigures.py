"""Editable SVG and matching PNG diagrams. MIT License, masafumi 2026."""
from pathlib import Path
from html import escape
import math
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'Docs/Assets/Images/PathTracing'
BG='#f4f7fb';INK='#172d48';MUTED='#52677e';BLUE='#2573c8';GOLD='#d78013';GREEN='#168974'
class Figure:
    def __init__(self,title,subtitle):
        self.image=Image.new('RGB',(1280,720),BG);self.draw=ImageDraw.Draw(self.image)
        self.svg=['<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="720" viewBox="0 0 1280 720">',f'<rect width="1280" height="720" fill="{BG}"/>']
        self.text(48,30,title,32);self.text(48,85,subtitle,19,MUTED)
    def text(self,x,y,s,size=22,color=INK):
        font=ImageFont.truetype('C:/Windows/Fonts/YuGothM.ttc',size)
        for i,line in enumerate(s.split('\n')):
            yy=y+i*(size+12);self.draw.text((x,yy),line,font=font,fill=color)
            self.svg.append(f'<text x="{x}" y="{yy+size}" font-family="Yu Gothic, sans-serif" font-size="{size}" fill="{color}">{escape(line)}</text>')
    def rect(self,x,y,w,h,color='white',stroke='#d2dce8'):
        self.draw.rounded_rectangle((x,y,x+w,y+h),radius=14,fill=color,outline=stroke,width=2)
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="14" fill="{color}" stroke="{stroke}" stroke-width="2"/>')
    def line(self,points,color=BLUE,width=4,arrow=True):
        self.draw.line(points,fill=color,width=width,joint='curve')
        self.svg.append(f'<polyline points="'+ ' '.join(f'{x},{y}' for x,y in points)+f'" fill="none" stroke="{color}" stroke-width="{width}"/>')
        if arrow:
            (a,b),(x,y)=points[-2:];angle=math.atan2(y-b,x-a);p=[(x,y),(x-15*math.cos(angle-.45),y-15*math.sin(angle-.45)),(x-15*math.cos(angle+.45),y-15*math.sin(angle+.45))]
            self.draw.polygon(p,fill=color);self.svg.append('<polygon points="'+' '.join(f'{xx},{yy}' for xx,yy in p)+f'" fill="{color}"/>')
    def dot(self,x,y,label='',color=BLUE):
        self.draw.ellipse((x-7,y-7,x+7,y+7),fill=color);self.svg.append(f'<circle cx="{x}" cy="{y}" r="7" fill="{color}"/>')
        if label:self.text(x-15,y+16,label,18)
    def save(self,name):
        OUT.mkdir(parents=True,exist_ok=True);self.image.save(OUT/(name+'.png'));(OUT/(name+'.svg')).write_text('\n'.join(self.svg+['</svg>']),encoding='utf-8')

def main():
    f=Figure('レイを使って、何を計算するのか','矢印はカメラから経路をたどる向き。パストレーシングもレイトレーシングを使います。')
    for i,title in enumerate(['影の可視性','1回の鏡面反射＋IBL','確率的な多重反射']):
        x=40+i*414;f.rect(x,145,390,510);f.text(x+20,165,title,24)
        f.rect(x+240,245,105,35,'#fff0cc',GOLD);f.text(x+250,247,'光源',20)
        f.line([(x+35,330),(x+140,505)]);f.dot(x+35,330,'カメラ');f.dot(x+140,505,'表面A')
        if i==0:
            f.line([(x+140,505),(x+280,285)],GOLD);f.text(x+25,575,'光源まで遮られるかを判定',19)
        elif i==1:
            f.line([(x+140,505),(x+290,405)],GREEN);f.dot(x+290,405,'表面B');f.line([(x+290,405),(x+290,285)],GOLD)
            f.text(x+25,575,'反射先の照明＋環境近似',19)
        else:
            f.line([(x+140,505),(x+300,440),(x+165,350),(x+280,285)],GREEN)
            f.dot(x+300,440,'B');f.dot(x+165,350,'C');f.text(x+25,575,'拡散・粗い反射を繰り返す',19)
    f.save('paths')
    f=Figure('反射方向を確率で選び、PDFで補正する','同じ方向の選択確率を、サンプリングとBRDF評価の両方で使います。')
    for i,title in enumerate(['Lambert：cosine-weighted sampling','GGX：visible-normal sampling']):
        x=45+i*620;f.rect(x,150,590,410);f.text(x+22,174,title,23)
        origin=(x+285,490);f.line([(x+70,500),(x+520,500)],MUTED,3,False)
        f.line([origin,(x+285,255)],MUTED,3);f.text(x+295,250,'N',20)
        angles=[-65,-40,-20,-5,8,25,45,67] if i==0 else [24,28,32,36,40,44,48,52]
        for angle in angles:
            a=math.radians(angle);f.line([origin,(origin[0]+190*math.sin(a),origin[1]-190*math.cos(a))],BLUE if i==0 else GREEN,3)
        if i==1:
            f.line([origin,(x+160,310)],MUTED,3);f.text(x+30,260,'V（前の頂点へ）',18,MUTED)
        f.dot(*origin)
    f.text(72,602,'throughput *= BRDF × cos(theta) / PDF',29)
    f.text(72,655,'多く選ぶ方向の寄与は割り引く。PDFが0の方向には寄与を加えません。',20,MUTED);f.save('sampling')
    f=Figure('NEEとMIS：光源へ到達する二つの選び方','同じ直接光を二重に加算せず、それぞれのPDFに応じて重みを付けます。')
    f.rect(45,145,740,510);f.rect(825,145,410,510)
    f.rect(425,205,270,45,'#fff0cc',GOLD);f.text(510,211,'面光源',24)
    f.dot(130,340,'カメラ');f.dot(330,540,'表面');f.line([(130,340),(330,540)])
    f.line([(330,540),(490,255)],GOLD,6);f.text(65,220,'NEE：光源上の点を選ぶ',22,GOLD)
    f.line([(330,540),(650,255)],GREEN,6);f.text(410,390,'BSDF：方向を選ぶ',21,GREEN)
    f.text(850,190,'面積PDF → 立体角PDF',21)
    f.text(850,240,'pL = p(select) × distance²\n       / (area × cos(light))',20)
    f.text(850,355,'power heuristic',24)
    f.text(850,405,'wL = pL² / (pL² + pB²)\nwB = pB² / (pL² + pB²)',22)
    f.text(850,540,'wL + wB = 1',28)
    f.text(850,595,'PDFは同じ測度で比較する',20,MUTED);f.save('nee-mis')
    f=Figure('線形HDRを蓄積してから表示へ変換する','静止中はサンプルを増やす。カメラやシーンが変わったら同じ画素として混ぜません。')
    boxes=[(45,'乱数を決める','画素・seed\nサンプル番号'),(350,'経路を追跡','新しい推定値 Ln\n1 spp / frame'),(655,'HDR平均を更新','RGBA32F\nサンプル数 n'),(960,'表示へ変換','露出・Reinhard\nsRGB・UI')]
    for x,title,detail in boxes:f.rect(x,240,270,215);f.text(x+18,260,title,24);f.text(x+18,330,detail,21,MUTED)
    for x in [315,620,925]:f.line([(x,345),(x+30,345)],BLUE)
    f.rect(45,510,575,140,'#e6f3ef',GREEN);f.text(66,525,'平均の更新',22);f.text(66,570,'mean += (Ln - mean) / (n + 1)',24)
    f.rect(655,510,575,140,'#fff0dd',GOLD);f.text(677,525,'カメラ・材質・照明・解像度 → Reset',21);f.text(677,573,'露出・UIだけの変更 → 蓄積を維持',21)
    f.save('accumulation')
if __name__=='__main__':main()
