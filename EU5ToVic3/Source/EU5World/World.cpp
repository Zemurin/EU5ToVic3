#include "World.h"
#include "CommonRegexes.h"
#include "Configuration.h"
#include "GameVersion.h"
#include "Log.h"
#include "ModLoader/ModNames.h"
#include "ParserHelpers.h"
#include "rakaly.h"
#include "zip.h"
#include <filesystem>
#include <fstream>
#include <string>

EU5::World::World(const std::shared_ptr<Configuration>& theConfiguration, const commonItems::ConverterVersion& converterVersion)
{
	Log(LogLevel::Info) << "*** Hello EU5, loading World. ***";
	EU5Path = theConfiguration->getEU5Path();
	saveGame.path = theConfiguration->getEU5SaveGamePath();
	Log(LogLevel::Progress) << "6 %";

	Log(LogLevel::Info) << "-> Verifying EU5 save.";
	// verifySave(); - disabling until rakaly, let's do it manually.
	saveSplitParser.registerKeyword("metadata", [this](std::istream& theStream) {
		saveGame.metadata = commonItems::stringOfItem(theStream).getString();
	});
	saveSplitParser.registerKeyword(commonItems::catchallRegex, [this](std::istream& theStream) {
		saveGame.gamestate += commonItems::stringOfItem(theStream).getString();
	});

	const std::ifstream saveFile(saveGame.path, std::ios::in | std::ios::binary);



	Log(LogLevel::Progress) << "7 %";

	Log(LogLevel::Progress) << "\t* Importing Save. *";
	auto metaData = std::istringstream(saveGame.metadata);
	auto gameState = std::istringstream(saveGame.gamestate);
	registerKeys(theConfiguration, converterVersion);
	parseStream(metaData);
	if (!saveGame.metadata.empty())
		saveGame.parsedMeta = true;
	parseStream(gameState);
	clearRegisteredKeywords();
	Log(LogLevel::Progress) << "\t* Import Complete. *";
	Log(LogLevel::Progress) << "15 %";

	// With mods loaded we can init stuff that requires them.
	modFS = commonItems::ModFilesystem(EU5Path, mods);

	Log(LogLevel::Info) << "-> Booting Loaders:";
	Log(LogLevel::Info) << "\t\tRegions";
	Log(LogLevel::Info) << "\t\tColonial Regions";
	Log(LogLevel::Info) << "\t\tTrade Companies";
	Log(LogLevel::Info) << "\t\tReligions";
	Log(LogLevel::Info) << "\t\tCultures";
	Log(LogLevel::Info) << "\t\tUnit Types";
	Log(LogLevel::Info) << "\t\tCommon Countries";
	Log(LogLevel::Info) << "\t\tLocalizations";
	Log(LogLevel::Progress) << "16 %";

	Log(LogLevel::Info) << "*** Building world ***";

	Log(LogLevel::Info) << "-> Classifying Provinces According to Aesthetic Principles";
	Log(LogLevel::Progress) << "17 %";

	Log(LogLevel::Info) << "-> Calculating Province Weights";
	Log(LogLevel::Progress) << "18 %";

	Log(LogLevel::Info) << "-> Determining Demographics";
	Log(LogLevel::Progress) << "19 %";

	Log(LogLevel::Info) << "-> Linking Provinces to Countries";
	Log(LogLevel::Progress) << "20 %";

	Log(LogLevel::Info) << "-> Updating Unit Types in Regiments";
	Log(LogLevel::Progress) << "21 %";

	Log(LogLevel::Info) << "-> Injecting Imperialism into Countries";
	Log(LogLevel::Progress) << "22 %";

	Log(LogLevel::Info) << "-> Injecting Art Deco into Countries";
	Log(LogLevel::Progress) << "23 %";

	Log(LogLevel::Info) << "-> Injecting Smokestacks into Provinces";
	Log(LogLevel::Progress) << "24 %";

	Log(LogLevel::Info) << "-> Injecting Localizations into Countries";
	Log(LogLevel::Progress) << "25 %";

	Log(LogLevel::Info) << "-> Viva la revolution?";
	Log(LogLevel::Progress) << "26 %";

	Log(LogLevel::Info) << "-> Merging Nations";
	Log(LogLevel::Progress) << "27 %";

	Log(LogLevel::Info) << "-> Cataloguing Native Fauna";
	Log(LogLevel::Info) << "-> Clasifying Invasive Fauna";
	Log(LogLevel::Progress) << "28 %";

	Log(LogLevel::Info) << "-> Dropping Dead, Empty and/or Coreless Nations";
	Log(LogLevel::Progress) << "29 %";

	Log(LogLevel::Info) << "-> Splitting Trade Companies into nations";
	Log(LogLevel::Progress) << "39 %";

	Log(LogLevel::Info) << "*** Good-bye EU4, you served us well. ***";
	Log(LogLevel::Progress) << "40 %";
}

void EU5::World::registerKeys(const std::shared_ptr<Configuration>& theConfiguration, const commonItems::ConverterVersion& converterVersion)
{
	registerKeyword("EU5txt", [](std::istream& theStream) {
	});
	registerKeyword("date", [this](std::istream& theStream) {
		if (saveGame.parsedMeta)
			commonItems::ignoreItem("unused", theStream);
		else
			datingData.lastEU4Date = date(commonItems::getString(theStream));
	});
	registerKeyword("start_date", [this](std::istream& theStream) {
		datingData.startEU4Date = date(commonItems::getString(theStream));
	});
	registerKeyword("savegame_version", [this, converterVersion](std::istream& theStream) {
		if (saveGame.parsedMeta)
		{
			commonItems::ignoreItem("unused", theStream);
			return;
		}

		version = GameVersion(theStream);
		Log(LogLevel::Info) << "Savegave version: " << version;

		if (converterVersion.getMinSource() > version)
		{
			Log(LogLevel::Error) << "Converter requires a minimum save from v" << converterVersion.getMinSource().toShortString();
			throw std::runtime_error("Savegame vs converter version mismatch!");
		}
		if (!converterVersion.getMaxSource().isLargerishThan(version))
		{
			Log(LogLevel::Error) << "Converter requires a maximum save from v" << converterVersion.getMaxSource().toShortString();
			throw std::runtime_error("Savegame vs converter version mismatch!");
		}
	});
	registerKeyword("mods_enabled_names", [this, theConfiguration](std::istream& theStream) {
		if (saveGame.parsedMeta)
		{
			commonItems::ignoreItem("unused", theStream);
			return;
		}

		Log(LogLevel::Info) << "-> Detecting used mods.";
		const auto& modBlobs = commonItems::blobList(theStream);
		Log(LogLevel::Info) << "<> Savegame claims " << modBlobs.getBlobs().size() << " mods used:";
		Mods incomingMods;
		for (const auto& modBlob: modBlobs.getBlobs())
		{
			auto modStream = std::stringstream(modBlob);
			const auto& modName = ModNames(modStream);
			incomingMods.emplace_back(Mod(modName.getName(), modName.getPath()));
			Log(LogLevel::Info) << "---> " << modName.getName() << ": " << modName.getPath();
		}

		// Let's locate, verify and potentially update those mods immediately.
		commonItems::ModLoader modLoader;
		modLoader.loadMods(theConfiguration->getEU5DocumentsPath(), incomingMods);
		mods = modLoader.getMods();
	});
	registerRegex(commonItems::catchallRegex, commonItems::ignoreItem);
}

void EU5::World::verifySave()
{
	const std::ifstream saveFile(saveGame.path, std::ios::in | std::ios::binary);
	std::stringstream inStream;
	inStream << saveFile.rdbuf();
	saveGame.gamestate = inStream.str();

	const auto save = rakaly::parseEu4(saveGame.gamestate);
	if (const auto& melt = save.meltMeta(); melt)
	{
		Log(LogLevel::Info) << "Meta extracted successfully.";
		melt->writeData(saveGame.metadata);
	}
	else if (save.is_binary())
	{
		Log(LogLevel::Error) << "Binary Save and NO META!";
	}

	if (save.is_binary())
	{
		Log(LogLevel::Info) << "Gamestate is binary, melting.";
		const auto& melt = save.melt();
		if (melt.has_unknown_tokens())
		{
			Log(LogLevel::Error) << "Rakaly reports errors while melting ironman save!";
		}

		melt.writeData(saveGame.gamestate);
	}
	else
	{
		Log(LogLevel::Info) << "Gamestate is textual.";
		const auto& melt = save.melt();
		melt.writeData(saveGame.gamestate);
	}

	zip_t* zip = zip_open(saveGame.path.string().c_str(), 0, 'r');
	const auto entriesCount = zip_entries_total(zip);
	if (entriesCount > 3)
		throw std::runtime_error("Unrecognized savegame structure!");

	// Always dump to disk for easier debug.
	std::ofstream metaDump("metaDump.txt");
	metaDump << saveGame.metadata;
	metaDump.close();

	std::ofstream saveDump("saveDump.txt");
	saveDump << saveGame.gamestate;
	saveDump.close();
}
