#include "copyright.h"


#ifndef _C4_H_
#define _C4_H_

#include "port.h"

extern "C" {

extern int16 C4WFXVal;
extern int16 C4WFYVal;
extern int16 C4WFZVal;
extern int16 C4WFX2Val;
extern int16 C4WFY2Val;
extern int16 C4WFDist;
extern int16 C4WFScale;

void C4TransfWireFrame();
void C4TransfWireFrame2();
void C4CalcWireFrame();

extern int16 C41FXVal;
extern int16 C41FYVal;
extern int16 C41FAngleRes;
extern int16 C41FDist;
extern int16 C41FDistVal;

void C4Op1F();
void C4Op15();
void C4Op0D();

extern int16 C4CosTable[];
extern int16 C4SinTable[];

}

#endif

