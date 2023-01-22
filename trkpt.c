/*=========================================================================
 *
 *   Filename:           trkpt.c
 *
 *   Author:             Marcelo Mourier
 *   Created:            Mon Apr 11 11:24:16 MDT 2022
 *
 *   Description:        Track point API
 *
 *=========================================================================
 *
 *                  Copyright (c) 2022 Marcelo Mourier
 *
 *=========================================================================
*/

#include <stdlib.h>

#include "const.h"
#include "trkpt.h"

TrkPt *nxtTrkPt(TrkPt **p1, TrkPt *p2)
{
    if (p1 != NULL)
        *p1 = p2;
    return TAILQ_NEXT(p2, tqEntry);
}

TrkPt *remTrkPt(GpsTrk *pTrk, TrkPt *p)
{
    TrkPt *nxt = TAILQ_NEXT(p, tqEntry);
    TAILQ_REMOVE(&pTrk->trkPtList, p, tqEntry);
    free(p);
    return nxt;
}

TrkPt *newTrkPt(int index, const char *inFile, int lineNum)
{
    TrkPt *pTrkPt;

    if ((pTrkPt = calloc(1, sizeof(TrkPt))) == NULL) {
        fprintf(stderr, "Failed to alloc TrkPt object !!!\n");
        return NULL;
    }

    pTrkPt->index = index;
    pTrkPt->inFile = inFile;
    pTrkPt->lineNum = lineNum;
    pTrkPt->elevation = nilElev;
    pTrkPt->speed = nilSpeed;
    pTrkPt->grade = nilGrade;

    return pTrkPt;
}

const char *fmtTrkPtIdx(const TrkPt *pTrkPt)
{
    static char fmtBuf[1024];

    snprintf(fmtBuf, sizeof (fmtBuf), "%s:%u", pTrkPt->inFile, pTrkPt->lineNum);

    return fmtBuf;
}

void printTrkPt(TrkPt *p)
{
    fprintf(stderr, "TrkPt #%u at %s {\n", p->index, fmtTrkPtIdx(p));
    fprintf(stderr, "  latitude=%.10lf longitude=%.10lf elevation=%.10lf time=%.3lf distance=%.10lf speed=%.10lf dist=%.10lf run=%.10lf rise=%.10lf grade=%.2lf\n",
            p->latitude, p->longitude, p->elevation, p->timestamp, p->distance, p->speed, p->dist, p->run, p->rise, p->grade);
    fprintf(stderr, "}\n");
}

// Dump the specified number of track points before and
// after the given TrkPt.
void dumpTrkPts(GpsTrk *pTrk, TrkPt *p, int numPtsBefore, int numPtsAfter)
{
    int i;
    TrkPt *tp;

    // Rewind numPtsBefore points...
    for (i = 0, tp = p; (i < numPtsBefore) && (tp != NULL); i++) {
        tp = TAILQ_PREV(tp, TrkPtList, tqEntry);
    }

    // Points before the given point
    while (tp != p) {
        printTrkPt(tp);
        tp = TAILQ_NEXT(tp, tqEntry);
    }

    // The point in question
    printTrkPt(p);

    // Points after the given point
    for (i = 0, tp = TAILQ_NEXT(p, tqEntry); (i < numPtsAfter) && (tp != NULL); i++, tp = TAILQ_NEXT(tp, tqEntry)) {
        printTrkPt(tp);
    }
}


