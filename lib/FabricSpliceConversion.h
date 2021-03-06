//
// Copyright (c) 2010-2017 Fabric Software Inc. All rights reserved.
//

#pragma once

#include <iostream>
#include <vector>
#include <map>
#include <maya/MDataBlock.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MFnData.h>
#include <maya/MFnNumericData.h>
#include <maya/MStringArray.h>
#include <maya/MDataHandle.h>

#include <FabricSplice.h>

#define MAYASPLICE_MEMORY_ALLOCATE(type, count) size_t valuesSize = sizeof(type) * count; type * values = (type*) malloc(valuesSize)
#define MAYASPLICE_MEMORY_SETITEM(index, value) values[index] = value
#define MAYASPLICE_MEMORY_GETITEM(index) values[index]
#define MAYASPLICE_MEMORY_SETPORT(port) port.setArrayData(values, valuesSize)
#define MAYASPLICE_MEMORY_GETPORT(port) port.getArrayData(values, valuesSize)
#define MAYASPLICE_MEMORY_FREE() free(values)

struct SpliceConversionTimers
{
  FabricSplice::Logging::AutoTimer * globalTimer;
  FabricSplice::Logging::AutoTimer * localTimer;

  SpliceConversionTimers()
  {
    globalTimer = NULL;
    localTimer = NULL;
  }

  void stop()
  {
    if(globalTimer != NULL)
      globalTimer->stop();
    if(localTimer != NULL)
      localTimer->stop();
  }

  void resume()
  {
    if(globalTimer != NULL)
      globalTimer->resume();
    if(localTimer != NULL)
      localTimer->resume();
  }
};

typedef void(*SplicePlugToPortFunc)(MPlug &plug, MDataBlock &data, FabricSplice::DGPort & port, SpliceConversionTimers * timers);
typedef void(*SplicePortToPlugFunc)(FabricSplice::DGPort & port, MPlug &plug, MDataBlock &data);

SplicePlugToPortFunc getSplicePlugToPortFunc(
  const std::string & dataType, 
  const FabricSplice::DGPort * port = NULL
  );
SplicePortToPlugFunc getSplicePortToPlugFunc(
  const std::string & dataType, 
  const FabricSplice::DGPort * port = NULL
  );
