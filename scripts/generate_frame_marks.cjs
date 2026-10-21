// Development only: npm package `sharp`. Generated PNGs are committed; the app
// does not need Node, SVG support, a network connection, or installed fonts.
const sharp = require('sharp');
const fs = require('node:fs/promises');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const sources = path.join(root, 'design/frame-marks/sources');
const output = path.join(root, 'app/assets/frame_marks');

async function main() {
  await fs.mkdir(output, {recursive: true});
  for (const id of ['sony', 'sony-alpha', 'hasselblad', 'hasselblad-h', 'fujifilm', 'sony-g', 'sony-gm', 'canon', 'nikon', 'sigma', 'tamron', 'zeiss', 'leica', 'panasonic', 'lumix', 'olympus', 'om-system', 'pentax', 'ricoh', 'dji', 'gopro', 'kodak']) {
    const extension = id === 'sony-g' || id === 'sony-gm' ? 'png' : 'svg';
    const source = path.join(sources, `${id}.${extension}`);
    const dimensions = await sharp(source).metadata();
    const density = Math.min(4800, Math.max(1, 72 * 1600 / Math.max(dimensions.width, dimensions.height)));
    const {data, info} = await sharp(source,
        extension === 'svg' ? {density} : {})
      .resize({width: 1600, height: 600, fit: 'inside'})
      .ensureAlpha().raw().toBuffer({resolveWithObject: true});
    // The G source has a white canvas. Keep its silver G in the original-colour
    // variant; use negative space for the monochrome variants.
    if (id === 'sony-g') {
      for (let i = 0; i < data.length; i += 4) {
        const low = Math.min(data[i], data[i + 1], data[i + 2]);
        if (low > 245) data[i + 3] = 0;
      }
    }
    for (let tone = 1; tone <= 3; ++tone) {
      const pixels = Buffer.from(data);
      for (let i = 0; i < pixels.length; i += 4) {
        if (tone === 1) continue;
        if (id === 'sony-g' || id === 'sony-gm' || id === 'zeiss' || id === 'leica') {
          const low = Math.min(pixels[i], pixels[i + 1], pixels[i + 2]);
          const range = id === 'sony-g' ? 211 : (id === 'zeiss' || id === 'leica') ? 255 : 210;
          pixels[i + 3] = Math.round(pixels[i + 3] * Math.max(0,
              Math.min(1, (range - low) / range)));
        }
        pixels[i] = pixels[i + 1] = pixels[i + 2] = tone === 2 ? 20 : 250;
      }
      await sharp(pixels, {raw: info}).trim({background: '#00000000', threshold: 2})
          .resize({width: 1024, height: 256, fit: 'inside'})
          .png().toFile(path.join(output, `${id}-${tone}.png`));
    }
  }
  // Series names are neutral typographic labels beside the genuine wordmark;
  // they are not reconstructions of official lens-barrel emblems.
  for (const [id, brand, label] of [
    ['canon-l','canon','L'], ['nikon-s','nikon','S LINE'],
    ['sigma-art','sigma','Art'], ['sigma-contemporary','sigma','Contemporary'],
    ['sigma-sports','sigma','Sports'],
  ]) {
    for (let tone=1; tone<=3; tone++) {
      const b=await sharp(path.join(output,`${brand}-${tone}.png`)).resize({height:72}).png().toBuffer();
      const bm=await sharp(b).metadata();
      const colour=tone===3?'#fafafa':tone===1 && id==='canon-l'?'#c8102e':'#141414';
      const text=await sharp({text:{text:`<span foreground="${colour}">${label}</span>`,
        font:'IBM Plex Mono 32',fontfile:path.join(root,'app/assets/frame_fonts/mono.ttf'),rgba:true}}).png().toBuffer();
      const tm=await sharp(text).metadata();
      await sharp({create:{width:bm.width+24+tm.width,height:96,channels:4,background:'#00000000'}})
        .composite([{input:b,left:0,top:12},{input:text,left:bm.width+24,top:Math.floor((96-tm.height)/2)}])
        .png().toFile(path.join(output,`${id}-${tone}.png`));
    }
  }

}
main().catch(error => { console.error(error); process.exitCode = 1; });
