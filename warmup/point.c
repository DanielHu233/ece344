#include <assert.h>
#include "common.h"
#include "point.h"
#include <math.h>

void
point_translate(struct point *p, double x, double y)
{
    p->x += x;
    p->y += y;
}

double
point_distance(const struct point *p1, const struct point *p2)
{
    double distance = sqrt(pow((p1->x - p2->x), 2) + pow((p1->y - p2->y), 2));
    return distance;
}

int
point_compare(const struct point *p1, const struct point *p2)
{
    double len1 = sqrt(pow((p1->x - 0.0), 2) + pow((p1->y - 0.0), 2));
    double len2 = sqrt(pow((p2->x - 0.0), 2) + pow((p2->y - 0.0), 2));
    if(len1 < len2) return -1;
    else if(len1 == len2) return 0;
    else return 1;
}
