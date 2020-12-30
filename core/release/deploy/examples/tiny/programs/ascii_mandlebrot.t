/*
 This is an integer ascii Mandelbrot generator
 */
left_edge   = -320;
right_edge  =  500;
top_edge    =  300;
bottom_edge = -300;
x_step      =    7;
y_step      =   15;

max_iter    =  200;

y0 = top_edge;
while (y0 > bottom_edge) {
	x0 = left_edge;
	while (x0 < right_edge) {
		y = 0;
		x = 0;
		the_char = ' ';
		i = 0;
		while (i < max_iter) {
			x_x = (x * x) / 200;
			y_y = (y * y) / 200;
			if (x_x + y_y > 800 ) {
				the_char = '0' + i;
				if (i > 9) {
					the_char = '@';
				}
				i = max_iter;
			}
			y = x * y / 100 + y0;
			x = x_x - y_y + x0;
			i = i + 1;
		}
		putc(the_char);
		x0 = x0 + x_step;
	}
	putc('\n');
	y0 = y0 - y_step;
}