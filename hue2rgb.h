#define HUE_TO_RGB(hue,red,grn,blu) \
do { \
	const double reg = hue / 60.0; \
	const double rmd = reg - floor(reg); \
	switch ((int) reg) { \
	case 0: \
		red = 1.0; \
		grn = rmd; \
		blu = 0; \
		break; \
	case 1: \
		red = 1.0 - rmd; \
		grn = 1.0; \
		blu = 0; \
		break; \
	case 2: \
		red = 0; \
		grn = 1.0; \
		blu = rmd; \
		break; \
	case 3: \
		red = 0; \
		grn = 1.0 - rmd; \
		blu = 1.0; \
		break; \
	case 4: \
		red = rmd; \
		grn = 0; \
		blu = 1.0; \
		break; \
	case 5: \
		red = 1.0; \
		grn = 0; \
		blu = 1.0 - rmd; \
		break; \
	case 6: \
		red = 1.0; \
		grn = rmd; \
		blu = 0; \
		break; \
	} \
} while (0) \

