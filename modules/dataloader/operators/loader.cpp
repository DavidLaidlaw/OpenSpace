/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2018                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <iostream>
#include <thread>
#include <string>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <regex>

#include <nfd.h>
#include <stdio.h>  // nfd
#include <stdlib.h> // nfd
#include <experimental/filesystem>

#include <modules/dataloader/operators/loader.h>
#include <ghoul/lua/lua_helper.h>
#include <ghoul/misc/dictionaryluaformatter.h>
#include <ghoul/misc/dictionaryjsonformatter.h>
#include <modules/dataloader/dataloadermodule.h>
#include <openspace/scene/scene.h>
#include <openspace/util/task.h>
#include <ghoul/logging/logmanager.h>
#include <openspace/properties/triggerproperty.h>
#include <openspace/scene/scenegraphnode.h>
#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <modules/dataloader/helpers.cpp>
#include <openspace/util/time.h>
#include <ghoul/filesystem/file.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/misc/dictionary.h>
#include <modules/kameleonvolume/kameleonvolumereader.h>
#include <ext/json/json.hpp>
#include <openspace/engine/openspaceengine.h>
#include <openspace/engine/globals.h>
#include <openspace/scene/assetmanager.h>

#include <ghoul/glm.h>

using Directory = ghoul::filesystem::Directory;
using File = ghoul::filesystem::File;
using RawPath = ghoul::filesystem::Directory::RawPath;
using PrettyPrint = ghoul::DictionaryLuaFormatter::PrettyPrint;
using Recursive = ghoul::filesystem::Directory::Recursive;
using TestResult = openspace::documentation::TestResult;
using json = nlohmann::json;

namespace
{
constexpr const char *_loggerCat = "Loader";
constexpr const char *KeyTime = "Time";
const std::string KeyRenderableType = "RenderableTimeVaryingVolume";
const std::string scaleTypeKey = "StaticScale";
const std::string translationTypeKey = "SpiceTranslation";
const std::string volumesGuiPathKey = "/Solar System/Volumes";
const std::string KeyVolumeToRawTask = "KameleonVolumeToRawTask";
constexpr const char *KeyVariable = "Variable";
const std::string KeyTfDestinationFileName = "transferfunction.txt";

const std::string KeyStepSize = "StepSize";
const std::string KeyGridType = "GridType";
const std::string KeySecondsAfter = "SecondsAfter";
const std::string KeySecondsBefore = "SecondsBefore";

constexpr const char *KeyTask = "Task";
constexpr const char *KeyItemName = "ItemName";
constexpr const char *KeyTransform = "Transform";
constexpr const char *KeyTranslation = "Translation";
constexpr const char *KeyPosition = "Position";
constexpr const char *KeyType = "Type";
constexpr const char *KeyObserver = "Observer";
constexpr const char *KeyTarget = "Target";
constexpr const char *KeyStaticTranslation = "StaticTranslation";
constexpr const char *KeyStaticScale = "StaticScale";
constexpr const char *KeyScale = "Scale";
constexpr const char *KeyTransferFunctionPath = "TransferFunctionPath";
constexpr const char *KeyTranslationValues = "TranslationValues";

constexpr const char *KeySourceDirectory = "SourceDirectory";
constexpr const char *KeyTransferFunction = "TransferFunction";
constexpr const char *KeyPath = "Path";
constexpr const char *KeyIdentifier = "Identifier";
constexpr const char *KeyParent = "Parent";
constexpr const char *KeyRenderable = "Renderable";
constexpr const char *KeyGUI = "GUI";

const double DefaultStepSize = 0.02;
const std::string DefaultGridType = "Spherical";
const double DefaultSecondsAfter = 24 * 60;
const double DefaultSecondsBefore = 24 * 60;
const float sunRadiusScale = 695508000;

} // namespace

namespace
{
static const openspace::properties::Property::PropertyInfo SelectedFilesInfo = {
    "SelectedFiles",
    "List of selected files and ready to load",
    "This list contains names of selected files in char format"};

static const openspace::properties::Property::PropertyInfo CurrentVolumesConvertedCountInfo = {
    "CurrentVolumesConvertedCount",
    "The amount of volumes currently converted",
    "This number indicates how many of the volumes currently selected "
    "for conversion have been converted."};

static const openspace::properties::Property::PropertyInfo CurrentVolumesToConvertCount = {
    "CurrentVolumesToConvertCount",
    "The amount of volumes to be converted",
    "This number indicates how many volumes are currently selected to be converted."};

static const openspace::properties::Property::PropertyInfo UploadDataTriggerInfo = {
    "UploadDataTrigger",
    "Trigger load data files",
    "If this property is triggered it will call the function to load data"};

static const openspace::properties::Property::PropertyInfo VolumeConversionProgressInfo = {
    "VolumeConversionProgress",
    "Progress value for volume conversion",
    "This float value between 0 and 1 corresponds to the progress of volume conversion"};

static const openspace::properties::Property::PropertyInfo VolumeMetaDataInfo = {
    "VolumeMetaData",
    "The metadata from the selected files",
    "This dictionary contains the metadata for the first of the selected volume files"};

static const openspace::properties::Property::PropertyInfo VolumeMetaDataJSONInfo = {
    "VolumeMetaDataJSON",
    "JSON formatted string of volume meta data",
    ""};

static const openspace::properties::Property::PropertyInfo ReadingNewMetaDataInfo = {
    "ReadingNewMetaData",
    "Boolean discerning if the process of reading volume meta data is finnished or not",
    ""};
} // namespace

namespace openspace::dataloader
{

Loader::Loader()
    : PropertyOwner({"Loader"}),
      _selectedFilePaths(SelectedFilesInfo),
      _currentVolumesConvertedCount(CurrentVolumesConvertedCountInfo),
      _currentVolumesToConvertCount(CurrentVolumesToConvertCount),
      _uploadDataTrigger(UploadDataTriggerInfo),
      _volumeConversionProgress(VolumeConversionProgressInfo),
      _volumeMetaDataJSON(VolumeMetaDataJSONInfo),
      _readingNewMetaData(ReadingNewMetaDataInfo)
{
    _uploadDataTrigger.onChange([this]() {
        selectData();
    });

    addProperty(_selectedFilePaths);
    addProperty(_currentVolumesConvertedCount);
    addProperty(_currentVolumesToConvertCount);
    addProperty(_uploadDataTrigger);
    addProperty(_volumeConversionProgress);
    addProperty(_volumeMetaDataJSON);
    addProperty(_readingNewMetaData);

    _volumeConversionThreadCanRun = false;
}

Loader::~Loader() {}

void Loader::selectData()
{
    {
        std::thread t([&]() {
            nfdpathset_t outPathSet;
            std::vector<std::string> paths;
            nfdresult_t result = NFD_OpenDialogMultiple("cdf", NULL, &outPathSet);

            size_t count = NFD_PathSet_GetCount(&outPathSet);
            if (count > 0 && result == NFD_OKAY)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    nfdchar_t *path = NFD_PathSet_GetPath(&outPathSet, i);
                    paths.push_back(static_cast<std::string>(path));
                }

                // TODO: reset function
                _volumeConversionProgress = FLT_MIN;
                _currentVolumesConvertedCount = 0;
                _volumeConversionThreadCanRun = false;
                _currentVolumesToConvertCount = count;
                _selectedFilePaths = paths;
                _readingNewMetaData = true;
                getVolumeMetaData();
                NFD_PathSet_Free(&outPathSet);
            }
            else if (result == NFD_CANCEL)
            {
                LINFO("User pressed cancel.");
            }
            else
            {
                std::string error = NFD_GetError();
                LINFO("Error: \n" + error);
            }
        });

        t.detach();
    }
}

//void Loader::createInternalDataItemProperties() {
//    module()->validateDataDirectory();
//    std::vector<std::string> volumeItems = module()->volumeDataItems();
//
//    // LDEBUG("volume items vec size " + std::to_string(volumeItems.size()));
//
//    for (auto item : volumeItems) {
//        const char* dirLeaf = openspace::dataloader::helpers::getDirLeaf(item).c_str();
//        const char* ItemTrigger = "ItemTrigger_";
//        const openspace::properties::Property::PropertyInfo info = {
//            ItemTrigger + dirLeaf,
//            dirLeaf,
//            ""
//        };
//
//        // Initialize trigger property with data item name (are they unique?)
//        auto volumeItemTrigger = properties::TriggerProperty(info);
//
//        // Set onChange method to call loadDataItem with the path as argument
//        volumeItemTrigger.onChange([this](){
//            // loadDataItem(item);
//        });
//
//        // addProperty(volumeItemTrigger);
//        // LDEBUG("Added property " + dirLeaf);
//    }
//}

// addDataItemProperty();
// removeDataItemProperties();

// void Loader::createVolumeDataItem(std::string absPath) {}

ghoul::Dictionary Loader::createAssetDictionary()
{
    // TODO: Variables to let user initialize in GUI
    std::string sunKey = "SUN";
    const float sunRadiusScale = 695508000;
    const float auScale = 149600000000;
    std::string parent = "SolarSystemBarycenter"; // valid for all volume data?

    ghoul::Dictionary translationDictionary;
    if (!_currentVolumeConversionDictionary.getValue<ghoul::Dictionary>(KeyTranslation, translationDictionary))
    {
        throw ghoul::RuntimeError("Must provide Translation table for volume conversion.");
    }

    // Read and create Scale Dictionary
    float scale = _currentVolumeConversionDictionary.value<float>(KeyScale) * sunRadiusScale;

    const std::string staticScale = "StaticScale";
    std::initializer_list<std::pair<std::string, ghoul::any>> scaleList = {
        std::make_pair(KeyType, staticScale),
        std::make_pair(KeyScale, scale)};
    ghoul::Dictionary scaleDictionary(scaleList);

    // Create Transform Dictionary
    std::initializer_list<std::pair<std::string, ghoul::any>> transformList = {
        std::make_pair(KeyTranslation, translationDictionary),
        std::make_pair(KeyScale, scaleDictionary)};
    ghoul::Dictionary transformDictionary(transformList);

    /*if (!renderableDictionary.hasKey(KeyTransform))
    {
        LERROR("Transform is not in renderable dictionary");
    }*/

    const std::string gridType = _currentVolumeConversionDictionary.value<std::string>(KeyGridType);
    const std::string itemName = _currentVolumeConversionDictionary.value<std::string>(KeyItemName);
    Directory d = getAssetFolderDirectory();
    std::string sourceDir = d.path();
    // const std::string sourceDirLeaf = openspace::dataloader::helpers::getDirLeaf(sourceDir);

    std::string tfFilePath = sourceDir +
                                   ghoul::filesystem::FileSystem::PathSeparator +
                                   KeyTfDestinationFileName;


    openspace::dataloader::helpers::replaceDoubleBackslashesWithForward(sourceDir);
    openspace::dataloader::helpers::replaceDoubleBackslashesWithForward(tfFilePath);

    const std::string renderableType = "RenderableTimeVaryingVolume";

    std::initializer_list<std::pair<std::string, ghoul::any>> renderableList = {
        std::make_pair(KeyType, renderableType),
        std::make_pair(KeySourceDirectory, sourceDir),
        std::make_pair(KeyTransferFunction, tfFilePath),
        std::make_pair(KeyStepSize, DefaultStepSize),
        std::make_pair(KeyGridType, gridType),
        std::make_pair(KeySecondsAfter, DefaultSecondsAfter),
        std::make_pair(KeySecondsBefore, DefaultSecondsBefore)};
    ghoul::Dictionary renderableDictionary(renderableList);

    std::initializer_list<std::pair<std::string, ghoul::any>> guiList = {
        std::make_pair(KeyPath, volumesGuiPathKey)};
    ghoul::Dictionary guiDictionary(guiList);

    std::initializer_list<std::pair<std::string, ghoul::any>> completeList = {
        std::make_pair(KeyIdentifier, itemName),
        std::make_pair(KeyParent, parent),
        std::make_pair(KeyRenderable, renderableDictionary),
        std::make_pair(KeyTransform, transformDictionary),
        std::make_pair(KeyGUI, guiDictionary)};
    ghoul::Dictionary completeDictionary(completeList);

    return completeDictionary;
}

void Loader::initializeNode(ghoul::Dictionary dict)
{
    SceneGraphNode *node = scene()->loadNode(dict);
    scene()->initializeNode(node);
}

void Loader::goToFirstTimeStep(const std::string &absPathToItem)
{
    std::string firstDictionaryFilePath = openspace::dataloader::helpers::getFileWithExtensionFromItemFolder(absPathToItem, "dictionary");
    ghoul::Dictionary dict = ghoul::lua::loadDictionaryFromFile(firstDictionaryFilePath);
    std::string firstTimeStep = dict.value<std::string>(KeyTime);
    setTime(firstTimeStep);
}

void Loader::getVolumeMetaData()
{
    std::vector<std::string> selectedFileVector = _selectedFilePaths;
    openspace::kameleonvolume::KameleonVolumeReader reader(selectedFileVector[0]);
    _volumeMetaDataDictionary = reader.readMetaData();

    ghoul::Dictionary gAttribDictionary;
    ghoul::Dictionary vAttribDictionary;
    const std::string KeyGlobalAttributes = "globalAttributes";
    const std::string KeyVariableAttributes = "variableAttributes";

    _volumeMetaDataDictionary.getValue<ghoul::Dictionary>(KeyGlobalAttributes, gAttribDictionary);
    _volumeMetaDataDictionary.getValue<ghoul::Dictionary>(KeyVariableAttributes, vAttribDictionary);

    constexpr const char *MetaDataModelNameKey = "model_name";
    constexpr const char *MetaDataGridSystemKey = "grid_system_1";
    constexpr const char *MetaDataGridSystemDimension1 = "grid_system_1_dimension_1_size";
    constexpr const char *MetaDataGridSystemDimension2 = "grid_system_1_dimension_2_size";
    constexpr const char *MetaDataGridSystemDimension3 = "grid_system_1_dimension_3_size";

    const std::vector<std::string> vKeys = vAttribDictionary.keys();
    const std::string modelName = gAttribDictionary.value<std::string>(MetaDataModelNameKey);
    std::string gridSystemString = gAttribDictionary.value<std::string>(MetaDataGridSystemKey);
    const int gridSystem1DimensionSize = gAttribDictionary.value<int>(MetaDataGridSystemDimension1);
    const int gridSystem2DimensionSize = gAttribDictionary.value<int>(MetaDataGridSystemDimension2);
    const int gridSystem3DimensionSize = gAttribDictionary.value<int>(MetaDataGridSystemDimension3);

    ghoul::DictionaryJsonFormatter formatter;
    const std::string variableAttributesJsonString = formatter.format(vAttribDictionary);

    std::string gridType;
    if (gridSystemString.find("theta") != std::string::npos && gridSystemString.find("phi") != std::string::npos)
    {
        gridType = "Spherical";
    }
    else
    {
        gridType = "Cartesian";
    }

    std::regex nonAlphanumeric("[^a-zA-Z0-9 $]+");
    gridSystemString = std::regex_replace(gridSystemString, nonAlphanumeric, " ");

    std::vector<std::string> gridSystem;
    std::istringstream iss(gridSystemString);
    for (std::string s; iss >> s;)
    {
        gridSystem.push_back(s);
    }

    std::string radiusUnit;
    std::vector<int> dataDimensions;
    if (modelName == "enlil")
    {
        dataDimensions = {64, 64, 64};
        radiusUnit = "AU";
    }
    else if (modelName == "batsrus")
    {
        dataDimensions = {128, 64, 64};
        radiusUnit = "Earth radius";
    }
    else if (modelName == "mas")
    {
        dataDimensions = {100, 100, 128};
        radiusUnit = "Sun radius";
    }
    else
    {
        dataDimensions = {100, 100, 100};
        radiusUnit = "";
    }

    json variableMaxBoundsJson;
    json variableMinBoundsJson;
    const std::string KeyActualMax = "actual_max";
    const std::string KeyActualMin = "actual_min";
    if (gridType == "Spherical")
    {
        const std::string KeyR = "r";

        ghoul::Dictionary rDictionary;
        vAttribDictionary.getValue<ghoul::Dictionary>(KeyR, rDictionary);

        const float actualMax = rDictionary.value<float>(KeyActualMax);
        const float actualMin = rDictionary.value<float>(KeyActualMin);
        const float AUmeters = 149597871000.0;

        //TODO Make cleaner!
        variableMinBoundsJson["r"] = actualMin / AUmeters;
        variableMaxBoundsJson["r"] = actualMax / AUmeters;
        variableMinBoundsJson["theta"] = -90;
        variableMaxBoundsJson["theta"] = 90;
        variableMinBoundsJson["phi"] = 0;
        variableMaxBoundsJson["phi"] = 360;
    }
    else
    {
        std::vector<std::string> keys = {"x", "y", "z"};
        for (auto key : keys)
        {
            ghoul::Dictionary dict;
            vAttribDictionary.getValue<ghoul::Dictionary>(key, dict);
            const float actualMax = dict.value<float>(KeyActualMax);
            const float actualMin = dict.value<float>(KeyActualMin);
            variableMaxBoundsJson[key] = actualMax;
            variableMinBoundsJson[key] = actualMin;
        }
    }

    json j;
    j["modelName"] = modelName;
    j["dataDimensions"] = {
        {gridSystem[0], dataDimensions[0]},
        {gridSystem[1], dataDimensions[1]},
        {gridSystem[2], dataDimensions[2]}};
    j["gridSystem"] = gridSystem;
    j["gridType"] = gridType;
    j["radiusUnit"] = radiusUnit;
    j["gridSystem1DimensionSize"] = gridSystem1DimensionSize;
    j["gridSystem2DimensionSize"] = gridSystem2DimensionSize;
    j["gridSystem3DimensionSize"] = gridSystem3DimensionSize;
    j["variableKeys"] = vKeys;
    j["variableMinBounds"] = variableMinBoundsJson;
    j["variableMaxBounds"] = variableMaxBoundsJson;
    j["variableAttributes"] = json::parse(variableAttributesJsonString);

    _readingNewMetaData = false;
    _volumeMetaDataJSON = j.dump();
}

void Loader::processCurrentlySelectedUploadData(const std::string &dictionaryString)
{
    _currentVolumeConversionDictionary = ghoul::lua::loadDictionaryFromString(dictionaryString);

    TestResult result;
    result.success = false;
    result = openspace::documentation::testSpecification(
        volumeConversionDocumentation(),
        _currentVolumeConversionDictionary);

    if (!result.success)
    {
        throw "Error in specification for volume conversion dictionary: " + dictionaryString;
    }
    else
    {
        _volumeConversionThreadCanRun = true;
    }

    // try {

    // {
    // std::thread t([&]() {
    // ??? won't work multithreaded
    // std::string testPath = "${DATA}" +
    //     ghoul::filesystem::FileSystem::PathSeparator;
    // testPath += ".internal";
    // LINFO(testPath);

    // Hold up thread until dictionary is loaded and valid
    //while (!_volumeConversionThreadCanRun)
    //{
    //}

    Directory d = getAssetFolderDirectory();
    FileSys.createDirectory(d);

    runConversionTask();
    copyTfFileToItemDir();
    ghoul::Dictionary assetDictionary = createAssetDictionary();

    const std::string assetFilePath = createAssetFile(assetDictionary);

    LINFO("Created files");


    _volumeConversionThreadCanRun = false;

    loadCreatedAsset(assetFilePath);

    //});

    //t.detach();
    //}

    // }
    // catch (const std::exception& e) {
    //     LWARNING("Caught exception for dictionaries");
    // }
}

void Loader::copyTfFileToItemDir()
{
    const std::string tfFileToCopyPath = _currentVolumeConversionDictionary.value<std::string>(KeyTransferFunctionPath);
    LDEBUG("tf path as it is: " + tfFileToCopyPath);
    LDEBUG("tf path abs path: " + absPath(tfFileToCopyPath));
    std::ifstream tfSource(absPath(tfFileToCopyPath));
    std::ofstream tfDest(absPath(getAssetFolderDirectory().path() + "/" + KeyTfDestinationFileName));
    if (!tfSource)
    {
        LERROR("Could not open source transferfunction file.");
    }
    if (!tfDest)
    {
        LERROR("Could not open destination transferfunction file.");
    }

    tfDest << tfSource.rdbuf();
    tfSource.close();
    tfDest.close();
}

void Loader::runConversionTask()
{

    ghoul::Dictionary taskDictionary;
    if (!_currentVolumeConversionDictionary.getValue<ghoul::Dictionary>(KeyTask, taskDictionary))
    {
        throw ghoul::RuntimeError("Must provide Task dictionary for volume conversion.");
    }

    std::mutex m;
    std::function<void(float)> cb = [&](float progress) {
        std::lock_guard<std::mutex> g(m);
        _volumeConversionProgress = progress;
    };

    std::vector<std::string> selectedFiles = _selectedFilePaths;
    unsigned int counter = 0;
    for (const std::string &file : selectedFiles)
    {
        _volumeConversionProgress = FLT_MIN;
        auto selectedFile = File(file);
        // Directory d = getAssetFolderDirectory();
        const std::string outputBasePath = getAssetFolderDirectory().path() + "/" + selectedFile.filename();

        const std::string rawVolumeOutput = outputBasePath + ".rawvolume";
        const std::string dictionaryOutput = outputBasePath + ".dictionary";

        taskDictionary.setValue("Type", KeyVolumeToRawTask);
        taskDictionary.setValue("Input", file);
        taskDictionary.setValue("RawVolumeOutput", rawVolumeOutput);
        taskDictionary.setValue("DictionaryOutput", dictionaryOutput);

        auto volumeToRawTask = Task::createFromDictionary(taskDictionary);

        volumeToRawTask->perform(cb);
        counter++;
        _currentVolumesConvertedCount = counter;
    }
}

Directory Loader::getAssetFolderDirectory()
{
    std::string itemName = _currentVolumeConversionDictionary.value<std::string>(KeyItemName);
    std::string itemPathBase = "${DATA}/.internal/volumes_from_cdf/" + itemName;
    Directory d(itemPathBase, RawPath::No);
    return d;
}

const std::string Loader::createAssetFile(ghoul::Dictionary assetDictionary)
{
    Directory d = getAssetFolderDirectory();
    const std::string path = d.path();

    ghoul::DictionaryLuaFormatter formatter(PrettyPrint::Yes, "  ");
    const std::string dictionaryString = formatter.format(assetDictionary);

    std::string identifier = assetDictionary.value<std::string>(KeyIdentifier);
    const std::string assetFilePath = absPath(path + "/" + identifier + ".asset");
    std::fstream fileStream(assetFilePath, std::ios::out);
    if (!fileStream)
    {
        LERROR("Could not create file");
    }

    const std::string importStatement = "local assetHelper = asset.require(\"util/asset_helper\")";
    const std::string spiceLine = "asset.require(\"spice/base\")";
    const std::string exportStatement = "assetHelper.registerSceneGraphNodesAndExport(asset, { " + identifier + " })";

    fileStream << importStatement << "\n"
               << spiceLine << "\n\n"
               << "local " << identifier << " = " << dictionaryString << "\n\n"
               << exportStatement << "\n";

    fileStream.close();

    return assetFilePath;
}

void Loader::loadCreatedAsset(const std::string& path) 
{
    global::openSpaceEngine.assetManager().add(path);
}

// void Loader::createVolumeDataItem(std::string absPath) {}

// ghoul::Dictionary Loader::createTaskDictionaryForOneVolumeItem(std::string inputPath, std::string outputBasePath) {

// defaults
// const int dimensions[3] = {100, 100, 128};
// const int lowerDomainBound[3] = {1, -90, 0};
// const int upperDomainBound[3] = {15, 90, 360};

// Set item dirLeaf as namelowerDomainBound
// const std::string itemName = openspace::dataloader::helpers::getDirLeaf(inputPath);
// const std::string itemOutputFilePath = outputBasePath +
//     ghoul::filesystem::FileSystem::PathSeparator +
//     itemName;

// const std::string RawVolumeOutput = itemOutputFilePath + ".rawvolume";
// const std::string DictionaryOutput = itemOutputFilePath + ".dictionary";

//     std::initializer_list<std::pair<std::string, ghoul::any>> list = {
//         std::make_pair( "Type", "KameleonVolumeToRawTask" ),
//         std::make_pair( "Input", inputPath ),
//         std::make_pair( "Dimensions", _uploadedDataDimensions ),
//         std::make_pair( "Variable", _uploadedDataVariable),
//         std::make_pair( "FactorRSquared", _uploadedDataFactorRSquared ),
//         std::make_pair( "LowerDomainBound", _uploadedDataLowerDomainBounds ),
//         std::make_pair( "UpperDomainBound", _uploadedDataHigherDomainBounds ),
//         std::make_pair( "RawVolumeOutput", RawVolumeOutput ),
//         std::make_pair( "DictionaryOutput", DictionaryOutput)
//     };

//     return ghoul::Dictionary(list);
// }

// ghoul::Dictionary Loader::createVolumeItemDictionary(std::string dataDictionaryPath, std::string dataStatePath) {

// }

documentation::Documentation Loader::volumeConversionDocumentation()
{
    using namespace documentation;
    return {
        "LoaderVolumeConversion",
        "",
        {{
             KeyItemName,
             new StringAnnotationVerifier("A name for the volume item"),
             Optional::No,
             "The volume item name",
         },
         {
             KeyGridType,
             new StringAnnotationVerifier("What type of grid for ray caster"),
             Optional::No,
             "What type of grid for ray caster",
         },
         {
             KeyTranslation,
             new TableAnnotationVerifier("A table with values for translation"),
             Optional::No,
             "The table with values for translation transform",
         },
         {
             KeyTask,
             new TableAnnotationVerifier("A table with values for volume conversion task"),
             Optional::No,
             "The table with values for volume conversion task",
         },
         {KeyTransferFunctionPath,
          new StringAnnotationVerifier("An absolute path to a transfer function file"),
          Optional::No,
          "An absolute path to a transfer function file to copy to the item directory"}}};
}

} // namespace openspace::dataloader
