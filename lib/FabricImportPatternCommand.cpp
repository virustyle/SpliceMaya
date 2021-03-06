//
// Copyright (c) 2010-2016, Fabric Software Inc. All rights reserved.
//

#include <QFileDialog>
#include <QDialog>

#include "Foundation.h"
#include "FabricImportPatternCommand.h"
#include "FabricDFGBaseInterface.h"
#include "FabricSpliceHelpers.h"
#include "FabricDFGWidget.h"
#include "FabricDFGConversion.h"
#include "FabricImportPatternDialog.h"

#include <maya/MStringArray.h>
#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>
#include <maya/MQtUtil.h>
#include <maya/MDGModifier.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MDagModifier.h>
#include <maya/MFnTransform.h>
#include <maya/MFnMesh.h>
#include <maya/MFnSet.h>
#include <maya/MFnLambertShader.h>
#include <maya/MCommandResult.h>

#include <FTL/FS.h>

MSyntax FabricImportPatternCommand::newSyntax()
{
  MSyntax syntax;
  syntax.enableQuery(false);
  syntax.enableEdit(false);
  syntax.addFlag( "-f", "-filepath", MSyntax::kString );
  syntax.addFlag( "-n", "-canvasnode", MSyntax::kString );
  syntax.addFlag( "-r", "-root", MSyntax::kString );
  syntax.addFlag( "-a", "-args", MSyntax::kString );
  syntax.addFlag( "-g", "-geometries", MSyntax::kString );
  return syntax;
}

void* FabricImportPatternCommand::creator()
{
  return new FabricImportPatternCommand;
}

MStatus FabricImportPatternCommand::doIt(const MArgList &args)
{
  MStatus status;
  MArgParser argParser(syntax(), args, &status);

  FabricDFGBaseInterface * interf = NULL;
  MString filepath;

  if ( argParser.isFlagSet("canvasnode") )
  {
    MString canvasnode = argParser.flagArgumentString("canvasnode", 0);
    interf = FabricDFGBaseInterface::getInstanceByName(canvasnode.asChar());
    if(interf == NULL)
    {
      mayaLogErrorFunc("Canvas node \""+canvasnode+"\" does no exist.");
      return mayaErrorOccured();
    }
  }

  if(interf == NULL)
  {
    if ( argParser.isFlagSet("filepath") )
    {
      filepath = argParser.flagArgumentString("filepath", 0);
    }
    else
    {
      MCommandResult result;
      MString command = "fileDialog2 -ff \"*.canvas\" -ds 2 -fm 1";
      MGlobal::executeCommand(command, result);
      if(result.resultType() == MCommandResult::kString)
      {
        result.getResult(filepath);
      }
      else if(result.resultType() == MCommandResult::kStringArray)
      {
        MStringArray filepaths;
        result.getResult(filepaths);

        if(filepaths.length() > 0)
          filepath = filepaths[0];
      }

      if(filepath.length() == 0)
      {
        mayaLogErrorFunc("No filepath specified.");
        return mayaErrorOccured();
      }
    }

    if(!FTL::FSExists(filepath.asChar()))
    {
      mayaLogErrorFunc("FilePath \""+filepath+"\" does no exist.");
      return mayaErrorOccured();
    }
  }

  if ( argParser.isFlagSet("root") )
  {
    m_rootPrefix = argParser.flagArgumentString("root", 0);
    if(m_rootPrefix.length() > 0)
    {
      if(m_rootPrefix.asChar()[m_rootPrefix.length()-1] != '/')
        m_rootPrefix += "/";
    }
  }

  MStringArray result;
  FabricCore::Client client;
  FabricCore::DFGBinding binding;

  if(interf != NULL)
  {
    try
    {
      client = interf->getCoreClient();
      client.loadExtension("GenericImporter", "", false);
      binding = interf->getDFGBinding();
    }
    catch(FabricSplice::Exception e)
    {
      mayaLogErrorFunc(MString(getName()) + ": "+e.what());
    }

    return invoke(binding, m_rootPrefix);
  }
  else
  {
    try
    {
      client = FabricDFGWidget::GetCoreClient();
      client.loadExtension("GenericImporter", "", false);
  
      MString json;
      FILE * file = fopen(filepath.asChar(), "rb");
      if(!file)
      {
        mayaLogErrorFunc("FilePath '"+filepath+"' cannot be opened.");
        return MS::kFailure;
      }
      else
      {
        fseek( file, 0, SEEK_END );
        long fileSize = ftell( file );
        rewind( file );

        char * buffer = (char*) malloc(fileSize + 1);
        buffer[fileSize] = '\0';

        size_t readBytes = fread(buffer, 1, fileSize, file);
        assert(readBytes == size_t(fileSize));
        (void)readBytes;

        fclose(file);

        json = buffer;
        free(buffer);
      }

      FabricCore::DFGHost dfgHost = client.getDFGHost();
      binding = dfgHost.createBindingFromJSON(json.asChar());

      std::map< std::string, MObject > geometryObjects;
      if ( argParser.isFlagSet("geometries") )
      {
        FabricCore::DFGExec exec = binding.getExec();

        MString geometries = argParser.flagArgumentString("geometries", 0);

        FTL::JSONStrWithLoc geometrySrcWithLoc( FTL::StrRef( geometries.asChar(), geometries.length() ) );
        FTL::OwnedPtr<FTL::JSONValue const> geometryValue( FTL::JSONValue::Decode( geometrySrcWithLoc ) );
        FTL::JSONObject const *geometryObject = geometryValue->cast<FTL::JSONObject>();

        for ( FTL::JSONObject::const_iterator it = geometryObject->begin(); it != geometryObject->end(); ++it )
        {
          FTL::CStrRef key = it->key();

          if(!it->value()->isString())
          {
            mayaLogFunc("Warning: Provided geometry "+MString(key.c_str())+" is not a string - needs to be the name of the geometry in the scene.");
            continue;
          }
          
          bool found = false;
          for(unsigned int i=0;i<exec.getExecPortCount();i++)
          {
            if(exec.getExecPortType(i) != FabricCore::DFGPortType_In)
              continue;

            FTL::CStrRef name = exec.getExecPortName(i);
            if(name != key)
              continue;

            FTL::CStrRef type = exec.getExecPortResolvedType(i);
            if(type != "PolygonMesh" && type != "Curves")
            {
              mayaLogFunc("Warning: Geometry "+MString(name.c_str())+" cannot be set since the canvas port is not valid geometry type.");
              continue;
            }

            MString geometryName = it->value()->getStringValue().c_str();
            MSelectionList sl;
            sl.add(geometryName);
            if(sl.length() == 0)
            {
              mayaLogErrorFunc("Provided geometry "+geometryName+" cannot be found in scene.");
              return mayaErrorOccured();
            }

            MDagPath dag;
            sl.getDagPath(0, dag);
            dag.extendToShape();
            MObject obj = dag.node();
            geometryObjects.insert( std::pair< std::string, MObject >(name.c_str(), obj));
            found = true;
            break;
          }

          if(!found)
          {
            mayaLogFunc("Geometry argument "+MString(key.c_str())+" does not exist in import pattern.");
            return mayaErrorOccured();
          }
        }
      }
      else
      {
        // parse the selection list for geometries
        MSelectionList sl;
        MGlobal::getActiveSelectionList(sl);

        MSelectionList geometries;
        for(unsigned int i=0;i<sl.length();i++)
        {
          MDagPath dag;
          sl.getDagPath(i, dag);
          dag.extendToShape();
          MObject obj = dag.node();

          MFnDependencyNode node(obj);
          MString typeName = node.typeName();

          if(typeName != "mesh" && typeName != "nurbsCurve")
            continue;

          geometries.add(dag.node());
        }
        
        unsigned int offset = 0;

        FabricCore::DFGExec exec = binding.getExec();
        for(unsigned int i=0;i<exec.getExecPortCount() && offset<geometries.length();i++)
        {
          if(exec.getExecPortType(i) != FabricCore::DFGPortType_In)
            continue;

          FTL::CStrRef type = exec.getExecPortResolvedType(i);
          if(type != "PolygonMesh" && type != "Curves")
          {
            continue;
          }

          MObject obj;
          geometries.getDependNode(offset, obj);
          MFnDependencyNode node(obj);

          FTL::CStrRef name = exec.getExecPortName(i);

          if(type == "PolygonMesh")
          {
            if(node.typeName() != "mesh")
            {
              mayaLogErrorFunc("Provided geometry "+node.name()+" is a "+node.typeName()+", but argument "+MString(name.c_str())+" is a "+MString(type.c_str())+".");
              return mayaErrorOccured();
            }
          }
          else if(type == "Curves")
          {
            if(node.typeName() != "nurbsCurve")
            {
              mayaLogErrorFunc("Provided geometry "+node.name()+" is a "+node.typeName()+", but argument "+MString(name.c_str())+" is a "+MString(type.c_str())+".");
              return mayaErrorOccured();
            }
          }

          geometryObjects.insert( std::pair< std::string, MObject >(name.c_str(), obj));
          offset++;
        }
      }

      for(std::map< std::string, MObject >::iterator it = geometryObjects.begin(); it != geometryObjects.end(); it++)
      {
        MString name = it->first.c_str();
        MObject obj = it->second;
        MFnDependencyNode node(obj);
        if(node.typeName() == "mesh")
        {
          MFnMesh mesh(obj);
          FabricCore::RTVal polygonMesh = FabricCore::RTVal::Create(client, "PolygonMesh", 0, 0);
          polygonMesh = dfgMFnMeshToPolygonMesh(mesh, polygonMesh);
          binding.setArgValue(name.asChar(), polygonMesh);
        }
        else if(node.typeName() == "nurbsCurve")
        {
          MFnNurbsCurve nurbsCurve(obj);
          FabricCore::RTVal curves = FabricCore::RTVal::Create(client, "Curves", 0, 0);
          FabricCore::RTVal curveCountRTVal = FabricSplice::constructUInt32RTVal( 1 );
          curves.callMethod( "", "setCurveCount", 1, &curveCountRTVal );
          dfgMFnNurbsCurveToCurves(0, nurbsCurve, curves);
          binding.setArgValue(name.asChar(), curves);
        }
      }

      if ( argParser.isFlagSet("args") )
      {
        FabricCore::DFGExec exec = binding.getExec();

        MString args = argParser.flagArgumentString("args", 0);

        FTL::JSONStrWithLoc argsSrcWithLoc( FTL::StrRef( args.asChar(), args.length() ) );
        FTL::OwnedPtr<FTL::JSONValue const> argsValue( FTL::JSONValue::Decode( argsSrcWithLoc ) );
        FTL::JSONObject const *argsObject = argsValue->cast<FTL::JSONObject>();

        for ( FTL::JSONObject::const_iterator it = argsObject->begin(); it != argsObject->end(); ++it )
        {
          FTL::CStrRef key = it->key();
          
          bool found = false;
          for(unsigned int i=0;i<exec.getExecPortCount();i++)
          {
            if(exec.getExecPortType(i) != FabricCore::DFGPortType_In)
              continue;

            FTL::CStrRef name = exec.getExecPortName(i);
            if(name != key)
              continue;

            FTL::CStrRef type = exec.getExecPortResolvedType(i);
            if(type != "String " && !FabricCore::GetRegisteredTypeIsShallow(client, type.c_str()))
            {
              mayaLogFunc("Warning: Argument "+MString(name.c_str())+" cannot be set since "+MString(type.c_str())+" is not shallow.");
              continue;
            }

            std::string json = it->value()->encode();
            FabricCore::RTVal value = FabricCore::ConstructRTValFromJSON(client, type.c_str(), json.c_str());
            binding.setArgValue(name.c_str(), value);
            found = true;
            break;
          }

          if(!found)
          {
            mayaLogFunc("Argument "+MString(key.c_str())+" does not exist in import pattern.");
            return mayaErrorOccured();
          }
        }

        return invoke(binding, m_rootPrefix);
      }
      else
      {
        QWidget * mainWindow = MQtUtil().mainWindow();
        mainWindow->setFocus(Qt::ActiveWindowFocusReason);

        QEvent event(QEvent::RequestSoftwareInputPanel);
        QApplication::sendEvent(mainWindow, &event);

        FabricImportPatternDialog * dialog = new FabricImportPatternDialog(mainWindow, binding, m_rootPrefix);
        dialog->setWindowModality(Qt::ApplicationModal);
        dialog->setSizeGripEnabled(true);
        dialog->open();

        if(!dialog->wasAccepted())
        {
          return MS::kSuccess;
        }
      }
    }
    catch(FabricSplice::Exception e)
    {
      mayaLogErrorFunc(MString(getName()) + ": "+e.what());
    }
  }

  return MS::kSuccess;
}

MStatus FabricImportPatternCommand::invoke(FabricCore::DFGBinding binding, MString rootPrefix)
{
  m_rootPrefix = rootPrefix;
  FabricCore::Context context;
  MStringArray result;
  try
  {
    context = binding.getHost().getContext();
    binding.execute();
  }
  catch(FabricSplice::Exception e)
  {
    mayaLogErrorFunc(MString(getName()) + ": "+e.what());

    if(binding.isValid())
    {
      MString errors = binding.getErrors(true).getCString();
      mayaLogErrorFunc(errors);
    }
    return mayaErrorOccured();
  }

  try
  {
    m_context = FabricCore::RTVal::Construct(context, "ImporterContext", 0, 0);
    FabricCore::RTVal contextHost = m_context.maybeGetMember("host");
    MString mayaVersion;
    mayaVersion.set(MAYA_API_VERSION);
    contextHost.setMember("name", FabricCore::RTVal::ConstructString(context, "Maya"));
    contextHost.setMember("version", FabricCore::RTVal::ConstructString(context, mayaVersion.asChar()));
    m_context.setMember("host", contextHost);

    FabricCore::DFGExec exec = binding.getExec();

    for(unsigned int i=0;i<exec.getExecPortCount();i++)
    {
      if(exec.getExecPortType(i) != FabricCore::DFGPortType_Out)
        continue;

      MString name = exec.getExecPortName(i);
      FabricCore::RTVal value = binding.getArgValue(name.asChar());
      MString resolvedType = value.getTypeNameCStr();
      if(resolvedType != "Ref<ImporterObject>[]")
        continue;

      for(unsigned j=0;j<value.getArraySize();j++)
      {
        FabricCore::RTVal obj = value.getArrayElement(j);
        obj = FabricCore::RTVal::Create(context, "ImporterObject", 1, &obj);

        MString path = obj.callMethod("String", "getInstancePath", 0, 0).getStringCString();
        path = m_rootPrefix + simplifyPath(path);

        m_objectMap.insert(std::pair< std::string, size_t > (path.asChar(), m_objectList.size()));
        m_objectList.push_back(obj);
      }
    }

    // ensure that we have the parents!
    for(size_t i=0;i<m_objectList.size();i++)
    {
      FabricCore::RTVal obj = m_objectList[i];
      FabricCore::RTVal transform = FabricCore::RTVal::Create(obj.getContext(), "ImporterTransform", 1, &obj);
      if(transform.isNullObject())
        continue;

      FabricCore::RTVal parent = transform.callMethod("ImporterObject", "getParent", 0, 0);
      if(parent.isNullObject())
        continue;

      MString path = parent.callMethod("String", "getInstancePath", 0, 0).getStringCString();
      path = m_rootPrefix + simplifyPath(path);

      std::map< std::string, size_t >::iterator it = m_objectMap.find(path.asChar());
      if(it != m_objectMap.end())
        continue;

      m_objectMap.insert(std::pair< std::string, size_t > (path.asChar(), m_objectList.size()));
      m_objectList.push_back(parent);
    }

    // create the groups first
    for(size_t i=0;i<m_objectList.size();i++)
    {
      getOrCreateNodeForObject(m_objectList[i]);
    }

    // now perform all remaining tasks
    for(size_t i=0;i<m_objectList.size();i++)
    {
      updateTransformForObject(m_objectList[i]);
      updateShapeForObject(m_objectList[i]);
      // todo: light, cameras etc..
    }
  }
  catch(FabricSplice::Exception e)
  {
    mayaLogErrorFunc(MString(getName()) + ": "+e.what());
  }

  for(std::map< std::string, MObject >::iterator it = m_nodes.begin(); it != m_nodes.end(); it++)
  {
    MFnDagNode node(it->second);
    result.append(node.fullPathName());
  }

  setResult(result);
  mayaLogFunc("import done.");
  return MS::kSuccess;
}


MString FabricImportPatternCommand::parentPath(MString path, MString * name)
{
  if(name)
    *name = path;  

  int rindex = path.rindex('/');
  if(rindex > 0)
  {
    if(name)
      *name = path.substring(rindex + 1, path.length()-1);
    return path.substring(0, rindex-1);
  }
  return "";
}

MString FabricImportPatternCommand::simplifyPath(MString path)
{
  if(path.length() == 0)
    return path;

  if (path.asChar()[0] == '/')
    path = path.substring(1, path.length() - 1);

  if(path.index('/') > 0)
  {
    MStringArray parts;
    path.split('/', parts);
    MString concat;
    for(unsigned int i=0;i<parts.length();i++)
    {
      if(concat.length() > 0)
        concat += "/";
      concat += simplifyPath(parts[i]);
    }
    return concat;
  }

  int rindex = path.rindex(':');
  if(rindex > 0)
    path = path.substring(rindex+1, path.length()-1);

  while(path.length() > 0)
  {
    if(path.asChar()[0] == '_' || isdigit(path.asChar()[0]))
    {
      path = path.substring(1, path.length()-1);
    }
    else
    {
      break;
    }
  }

  return path;
}

MObject FabricImportPatternCommand::getOrCreateNodeForPath(MString path, MString type, bool createIfMissing)
{
  if(path.length() == 0)
    return MObject();

  path = simplifyPath(path);

  std::map< std::string, MObject >::iterator it = m_nodes.find(path.asChar());
  if(it != m_nodes.end())
    return it->second;

  if(!createIfMissing)
    return MObject();

  MString name = path;
  MObject parentNode;
  int rindex = path.rindex('/');
  if(rindex > 0)
  {
    MString pathForParent = parentPath(path, &name);
    parentNode = getOrCreateNodeForPath(pathForParent, "transform", true);
  }

  MDagModifier modif;
  MObject node = modif.createNode(type, parentNode);
  modif.doIt();

  modif.renameNode(node, name);
  modif.doIt();

  m_nodes.insert(std::pair< std::string, MObject > (path.asChar(), node));
  return node;
}

MObject FabricImportPatternCommand::getOrCreateNodeForObject(FabricCore::RTVal obj)
{
  MString type = obj.callMethod("String", "getType", 0, 0).getStringCString();
  MString path = obj.callMethod("String", "getInstancePath", 0, 0).getStringCString();
  path = m_rootPrefix + simplifyPath(path);

  if(type == "Layer" || type == "Group" || type == "Transform" || type == "Instance")
  {
    return getOrCreateNodeForPath(path, "transform");
  }
  else if(type == "Shape")
  {
    // done by updateShapeForObject
    return MObject();
  }
  else if(type == "Light")
  {
    // todo
    return MObject();
  }
  else if(type == "Camera")
  {
    // todo
    return MObject();
  }
  else if(type == "Material" || type == "Texture")
  {
    // this is expected - no groups for you!
    return MObject();
  }
  else
  {
    mayaLogErrorFunc("Unsupported ImporterObject type "+type+" for "+path);
    return MObject();
  }

  return MObject();
}

bool FabricImportPatternCommand::updateTransformForObject(FabricCore::RTVal obj, MObject node)
{
  FabricCore::RTVal transform = FabricCore::RTVal::Create(obj.getContext(), "ImporterTransform", 1, &obj);
  if(transform.isNullObject())
    return false;

  if(node.isNull())
  {
    MString path = obj.callMethod("String", "getInstancePath", 0, 0).getStringCString();
    path = m_rootPrefix + simplifyPath(path);

    node = getOrCreateNodeForPath(path, "transform", false);
    if(node.isNull())
      return false;
  }

  MFnTransform transformNode(node);

  FabricCore::RTVal localTransform = transform.callMethod("Mat44", "getLocalTransform", 1, &m_context);
  void * data = localTransform.callMethod("Data", "data", 0, 0).getData();
  float floats[4][4];
  memcpy(floats, data, sizeof(float) * 16);

  MTransformationMatrix tfMatrix = MMatrix(floats).transpose();
  
  transformNode.set(tfMatrix);

  return true;
}

bool FabricImportPatternCommand::updateShapeForObject(FabricCore::RTVal obj)
{
  FabricCore::RTVal shape = FabricCore::RTVal::Create(obj.getContext(), "ImporterShape", 1, &obj);
  if(shape.isNullObject())
    return false;

  //const Integer ImporterShape_Mesh = 0;
  //const Integer ImporterShape_Curves = 1;
  //const Integer ImporterShape_Points = 2;
  FabricCore::RTVal geoTypeVal = shape.callMethod("SInt32", "getGeometryType", 1, &m_context);
  int geoType = geoTypeVal.getSInt32();
  if (geoType != 0) // not a mesh
  {
    MString geoTypeStr;
    geoTypeStr.set(geoType);
    mayaLogFunc("Shape type " + geoTypeStr + " not yet supported.");
    return false;
  }

  FabricCore::RTVal isConstantVal = shape.callMethod("Boolean", "isConstant", 1, &m_context);
  if(!isConstantVal.getBoolean())
  {
    mayaLogFunc("Deforming shapes are not yet supported.");
    return false;
  }

  // here we access the path as well as the instance path
  MString uuid = obj.callMethod("String", "getPath", 0, 0).getStringCString();
  uuid = "uuid | " + uuid;

  MString instancePath = obj.callMethod("String", "getInstancePath", 0, 0).getStringCString();
  instancePath = m_rootPrefix + simplifyPath(instancePath);

  MString name;
  instancePath = parentPath(instancePath, &name);
  MObject parentNode = getOrCreateNodeForPath(instancePath, "transform", false);

  // now check if we have already converted this shape before
  std::map< std::string, MObject >::iterator it = m_nodes.find(uuid.asChar());
  MObject node;
  if(it == m_nodes.end())
  {
    FabricCore::RTVal polygonMesh = shape.callMethod("PolygonMesh", "getGeometry", 1, &m_context);
    if (polygonMesh.isNullObject())
      return false;

    node = dfgPolygonMeshToMFnMesh(polygonMesh, false /* insideCompute */);
    m_nodes.insert(std::pair< std::string, MObject > (uuid.asChar(), node));

    MDagModifier modif;
    modif.renameNode(node, name);
    modif.doIt();
  
    if(!parentNode.isNull())
    {
      modif.reparentNode(node, parentNode);
      modif.doIt();
    }

    updateTransformForObject(obj, node);
    updateMaterialForObject(obj, node);
  }
  else if(!parentNode.isNull())
  {
    node = it->second;

    MFnDagNode parentDag(parentNode);
    parentDag.addChild(node, MFnDagNode::kNextPos, true /* keepExistingParents */);
  }

  return true;
}

bool FabricImportPatternCommand::updateMaterialForObject(FabricCore::RTVal obj, MObject node)
{
  MStatus status;
  FabricCore::RTVal shape = FabricCore::RTVal::Create(obj.getContext(), "ImporterShape", 1, &obj);
  if(shape.isNullObject())
    return false;

  if(node.isNull())
    return false;

  FabricCore::RTVal materials = shape.callMethod("Ref<ImporterObject>[]", "getMaterials", 1, &m_context);

  MString materialName;
  MColor materialColor(0.7f, 0.7f, 0.7f);

  if (materials.getArraySize() > 0)
  {
    // todo: for now we only support one material...
    FabricCore::RTVal material = materials.getArrayElement(0);
    materialName = material.callMethod("String", "getName", 0, 0).getStringCString();

    for(unsigned int i=0;i<2;i++)
    {
      FabricCore::RTVal propertyNameVal;
      if(i == 0)
        propertyNameVal = FabricCore::RTVal::ConstructString(obj.getContext(), "diffuse");
      else
        propertyNameVal = FabricCore::RTVal::ConstructString(obj.getContext(), "color");

      MString propType = material.callMethod("String", "getPropertyType", 1, &propertyNameVal).getStringCString();
      if(propType != "Color")
        continue;

      FabricCore::RTVal args[2];
      args[0] = propertyNameVal;
      args[1] = m_context;
      material.callMethod("", "updateProperty", 2, args);
      FabricCore::RTVal colorVal = material.callMethod("Color", "getColorProperty", 1, &propertyNameVal);

      materialColor.r = colorVal.maybeGetMember("r").getFloat32();
      materialColor.g = colorVal.maybeGetMember("g").getFloat32();
      materialColor.b = colorVal.maybeGetMember("b").getFloat32();
      materialColor.a = colorVal.maybeGetMember("a").getFloat32();
    }
  }
  else
  {
    FabricCore::RTVal colorVal = shape.callMethod("Color", "getColor", 1, &m_context);
    materialColor.r = colorVal.maybeGetMember("r").getFloat32();
    materialColor.g = colorVal.maybeGetMember("g").getFloat32();
    materialColor.b = colorVal.maybeGetMember("b").getFloat32();
    materialColor.a = 1.0;

    MString r, g, b;
    r.set(int((float)materialColor.r * 255.0f + 0.5f));
    g.set(int((float)materialColor.g * 255.0f + 0.5f));
    b.set(int((float)materialColor.b * 255.0f + 0.5f));

    materialName = "Material_r"+r+"g"+g+"b"+b+"Color";
  }

  MString sgName = materialName + "SG";

  MObject shadingEngine;
  std::map< std::string, MObject >::iterator it = m_materialSets.find(sgName.asChar());
  if(it == m_materialSets.end())
  {
    MSelectionList sl;
    sl.add(sgName);

    if(sl.length() == 0)
    {
      MString shaderName;
      MString shadingEngineName;
      MGlobal::executeCommand("sets -renderable true -noSurfaceShader true -empty -name \""+sgName+"\";", shadingEngineName);
      MGlobal::executeCommand("shadingNode -asShader lambert -name \"" + materialName + "\";", shaderName);
      MGlobal::executeCommand("connectAttr -f \""+shaderName+".outColor\" \""+ shadingEngineName +".surfaceShader\";");

      MString r, g, b;
      r.set(materialColor.r);
      g.set(materialColor.g);
      b.set(materialColor.b);
      MGlobal::executeCommand("setAttr \""+shaderName+".color\" -type double3 "+r+" "+g+" "+b+";");

      sl.add(shadingEngineName);
      sl.getDependNode(0, shadingEngine);
      m_materialSets.insert(std::pair< std::string, MObject > (shadingEngineName.asChar(), shadingEngine));
    }
    else
    {
      sl.getDependNode(0, shadingEngine);
    }
  }
  else
  {
    shadingEngine = it->second;
  }

  MFnSet shadingEngineSet(shadingEngine);
  shadingEngineSet.addMember(node);

  return true;
}
