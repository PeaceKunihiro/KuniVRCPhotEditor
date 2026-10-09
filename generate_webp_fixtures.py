"""Development-only synthetic fixtures; Pillow is not a build/runtime dependency."""
from PIL import Image
from io import BytesIO
from pathlib import Path

xmp = '''<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"><rdf:Description xmlns:xmp="http://ns.adobe.com/xap/1.0/" xmlns:vrc="http://ns.vrchat.com/vrc/1.0/" xmp:CreateDate="2026-10-09T12:34:56Z" xmp:Author="WebP撮影者" vrc:WorldDisplayName="WebPワールド" vrc:WorldID="wrld_webp"/></rdf:RDF>'''.encode()
image = Image.new('RGBA', (320,160), (200,100,50,128))
arrays = []
for name, lossless in [('LosslessWebP',True), ('LossyWebP',False)]:
    stream = BytesIO()
    image.save(stream, format='WEBP', lossless=lossless, xmp=xmp)
    arrays.append((name, stream.getvalue()))
stream = BytesIO()
image.save(stream, format='WEBP', save_all=True, append_images=[Image.new('RGBA',(320,160),(0,200,80,255))], duration=[100,100], loop=0, lossless=True)
arrays.append(('AnimatedWebP', stream.getvalue()))
Path(__file__).with_name('WebPFixtures.h').write_text('#pragma once\n' + '\n'.join('const unsigned char '+name+'[] = {'+','.join(str(b) for b in data)+'};' for name,data in arrays) + '\n', encoding='utf-8')
