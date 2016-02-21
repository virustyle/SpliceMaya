//
// Copyright (c) 2010-2016, Fabric Software Inc. All rights reserved.
//

#ifndef __FABRIC_SPLICE_RENDER_CALLBACK_H__
#define __FABRIC_SPLICE_RENDER_CALLBACK_H__

#include "FabricSpliceHelpers.h"
#include "FabricDFGBaseInterface.h"
#include "FabricSpliceBaseInterface.h"
 
#include <cmath> 
#include <iostream>

#include "Foundation.h"
#include <FabricCore.h>
#include <FabricSplice.h>

#include <maya/MColor.h>
#include <maya/MString.h>
#include <maya/M3dView.h>
#include <maya/MGlobal.h>
#include <maya/M3dView.h>
#include <maya/MMatrix.h>
#include <maya/MDagPath.h>
#include <maya/MFnCamera.h>
#include <maya/MUiMessage.h>
#include <maya/MFnDagNode.h>
#include <maya/MAnimControl.h>
#include <maya/MDrawContext.h>
#include <maya/MFrameContext.h>
#include <maya/MViewport2Renderer.h>
 

class FabricSpliceRenderCallback {

  public:
    static bool gCallbackEnabled;
    static FabricCore::RTVal sRTROGLHostCallback;
    static FabricCore::RTVal sDrawContext;

    static void enable(bool enable);
    static void disable();
    static bool isEnabled();
    static void plug(); 
    static void unplug();

    static bool canDraw();
    static void draw(double width, double height, const MString &str, uint32_t phase);
};


#endif // __FABRIC_SPLICE_RENDER_CALLBACK_H__