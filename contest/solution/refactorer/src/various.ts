
// Color functions adapted from: https://css-tricks.com/converting-color-spaces-in-javascript/
export type RGB = { r: number, g: number, b: number };
export type HSL = { h: number, s: number, l: number };


export function RGBToHex(rgb: RGB): string {
	const { r, g, b } = rgb;

	let rs = r.toString(16);
	let gs = g.toString(16);
	let bs = b.toString(16);
  
	if (rs.length == 1)
	  rs = "0" + rs;
	if (gs.length == 1)
	  gs = "0" + gs;
	if (bs.length == 1)
	  bs = "0" + bs;
  
	return "#" + r + g + b;
}


export function HSLToRGB(hsl: HSL): RGB {
	let { h, s, l } = hsl;

	// Must be fractions of 1
	s /= 100;
	l /= 100;
  
	let c = (1 - Math.abs(2 * l - 1)) * s,
		x = c * (1 - Math.abs((h / 60) % 2 - 1)),
		m = l - c/2,
		r = 0,
		g = 0,
		b = 0;

	if (0 <= h && h < 60) {
		r = c; g = x; b = 0;  
	} else if (60 <= h && h < 120) {
		r = x; g = c; b = 0;
	} else if (120 <= h && h < 180) {
		r = 0; g = c; b = x;
	} else if (180 <= h && h < 240) {
		r = 0; g = x; b = c;
	} else if (240 <= h && h < 300) {
		r = x; g = 0; b = c;
	} else if (300 <= h && h < 360) {
		r = c; g = 0; b = x;
	}
	r = Math.round((r + m) * 255);
	g = Math.round((g + m) * 255);
	b = Math.round((b + m) * 255);
		
	return { r, g, b };
	// return "rgb(" + r + "," + g + "," + b + ")";
}


export function HSLToHex(hsl: HSL): string {
	return RGBToHex(HSLToRGB(hsl));
}




// Hash function from: https://gist.github.com/hyamamoto/fd435505d29ebfa3d9716fd2be8d42f0?permalink_comment_id=2694461#gistcomment-2694461
export function stringHash(s: string) {
	for(var i = 0, h = 0; i < s.length; i++)
		h = Math.imul(31, h) + s.charCodeAt(i) | 0;
	return h;
}
