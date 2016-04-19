#include "common.hpp"

extern "C" {

bool simple_analyze(unsigned int *inpixels, unsigned int width, unsigned int height, int (&mark)[MAX_MARK_COUNT][3], int &markcount, int (&prcount)[4]);

}
