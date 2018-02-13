/***************************************************************************
 *            skyscraper.cpp
 *
 *  Wed Jun 7 12:00:00 CEST 2017
 *  Copyright 2017 Lars Muldjord
 *  muldjordlars@gmail.com
 ****************************************************************************/
/*
 *  This file is part of skyscraper.
 *
 *  skyscraper is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  skyscraper is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with skyscraper; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <iostream>

#include <QThread>
#include <QSettings>
#include <QDirIterator>
#include <QTimer>

#include "skyscraper.h"
#include "xmlreader.h"
#include "strtools.h"

#include "emulationstation.h"
#include "attractmode.h"

Skyscraper::Skyscraper(const QCommandLineParser &parser, const QString &currentDir)
{
  qRegisterMetaType<GameEntry>("GameEntry");
  
  printf("\033[1;34m------------------------------------------\033[0m\n\033[1;33mRunning Skyscraper v" VERSION " by Lars Muldjord\033[0m\n\033[1;34m------------------------------------------\033[0m\n");

  config.currentDir = currentDir;
  loadConfig(parser);
}

Skyscraper::~Skyscraper()
{
  delete frontend;
}

void Skyscraper::run()
{
  printf("Platform           : '\033[1;32m%s\033[0m'\n", config.platform.toStdString().c_str());
  printf("Scraper module     : '\033[1;32m%s\033[0m'\n", config.scraper.toStdString().c_str());
  if(config.emulator != "") {
    printf("Emulator           : '\033[1;32m%s\033[0m'\n", config.emulator.toStdString().c_str());
  }
  printf("Input folder       : '\033[1;32m%s\033[0m'\n", config.inputFolder.toStdString().c_str());
  printf("Game list folder   : '\033[1;32m%s\033[0m'\n", config.gameListFolder.toStdString().c_str());
  printf("Covers folder      : '\033[1;32m%s\033[0m'\n", config.coversFolder.toStdString().c_str());
  printf("Screenshots folder : '\033[1;32m%s\033[0m'\n", config.screenshotsFolder.toStdString().c_str());
  printf("Wheels folder      : '\033[1;32m%s\033[0m'\n", config.wheelsFolder.toStdString().c_str());
  printf("Marquees folder    : '\033[1;32m%s\033[0m'\n", config.marqueesFolder.toStdString().c_str());
  if(config.localDb) {
    printf("Local db folder    : '\033[1;32m%s\033[0m'\n\n", config.dbFolder.toStdString().c_str());
  }
  
  if(config.scraper == "arcadedb" && config.threads != 1) {
    printf("Forcing 1 thread to accomodate limits in ArcadeDB scraping module\n\n");
    config.threads = 1;
  }

  doneThreads = 0;
  notFound = 0;
  found = 0;
  avgCompleteness = 0;
  avgSearchMatch = 0;
  
  if(!config.dbFolder.isEmpty() && config.localDb) {
    localDb = QSharedPointer<LocalDb>(new LocalDb(config.dbFolder));
    if(localDb->createFolders(config.scraper)) {
      localDb->readDb();
    } else {
      printf("Couldn't create local db folders, disabling localdb...\n");
      config.localDb = false;
    }
  }
  if(config.localDb && config.cleanDb) {
    localDb->cleanDb();
    localDb->writeDb();
    exit(0);
  }
  if(config.localDb && !config.mergeDb.isEmpty() && QDir(config.mergeDb).exists()) {
    LocalDb srcDb(config.mergeDb);
    srcDb.readDb();
    localDb->mergeDb(srcDb, config.updateDb, config.mergeDb);
    localDb->writeDb();
    exit(0);
  }
  if(config.localDb) {
    localDb->readPriorities();
  }

  QDir inputDir(config.inputFolder, Platform::getFormats(config.platform), QDir::Name, QDir::Files);
  if(!inputDir.exists()) {
    printf("Input folder '\033[1;32m%s\033[0m' doesn't exist or can't be seen by current user. Please check path and permissions.\n", inputDir.absolutePath().toStdString().c_str());
    exit(1);
  }

  QDir gameListDir(config.gameListFolder);
  checkForFolder(gameListDir);
  config.gameListFolder = gameListDir.absolutePath();
  
  QDir coversDir(config.coversFolder);
  checkForFolder(coversDir);
  config.coversFolder = coversDir.absolutePath();

  QDir screenshotsDir(config.screenshotsFolder);
  checkForFolder(screenshotsDir);
  config.screenshotsFolder = screenshotsDir.absolutePath();

  QDir wheelsDir(config.wheelsFolder);
  checkForFolder(wheelsDir);
  config.wheelsFolder = wheelsDir.absolutePath();

  QDir marqueesDir(config.marqueesFolder);
  checkForFolder(marqueesDir);
  config.marqueesFolder = marqueesDir.absolutePath();

  if(config.videos) {
    QDir videosDir(config.videosFolder);
    checkForFolder(videosDir);
    config.videosFolder = videosDir.absolutePath();
  }

  gameListFileString = gameListDir.absolutePath() + "/" + frontend->getGameListFileName();

  QFile gameListFile(gameListFileString);

  if(!config.pretend && !config.unattend && gameListFile.exists()) {
    std::string userInput = "";
    printf("\033[1;34m'\033[1;32m%s\033[0m\033[1;34m' already exists, do you want to overwrite it\033[0m (y/N)? ", frontend->getGameListFileName().toStdString().c_str());
    getline(std::cin, userInput);
    if(userInput == "y" || userInput == "Y") {
    } else {
      printf("User chose not to overwrite, now exiting...\n");
      exit(0);
    }
    
    printf("Checking if '\033[1;32m%s\033[0m' is writable?... ", frontend->getGameListFileName().toStdString().c_str());
    
    if(gameListFile.open(QIODevice::Append)) {
      printf("\033[1;32mIt is! :)\033[0m\n");
      gameListFile.close();
    } else {
      printf("\033[1;31mIt isn't! :(\nPlease check path and permissions and try again.\033[0m\n");
      exit(1);
    }
  }
  if(config.pretend) {
    printf("Pretend set! Not changing any game list and media files, but still caching resources in local database.\n\n");
  }

  QFile skippedFile(skippedFileString);
  skippedFile.open(QIODevice::WriteOnly);
  skippedFile.write("--- The following is a list of skipped games ---\n");
  skippedFile.close();

  QList<QFileInfo> inputFiles = inputDir.entryInfoList();
  if(config.subDirs) {
    QDirIterator dirIt(config.inputFolder,
		       QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
		       QDirIterator::Subdirectories);
    while(dirIt.hasNext()) {
      QString subdir = dirIt.next();
      inputDir.setPath(subdir);
      inputFiles.append(inputDir.entryInfoList());
      if(config.verbose) {
	printf("Added files from subdir: '%s'\n", subdir.toStdString().c_str());
      }
    }
  }

  if(!cliFiles.isEmpty()) {
    inputFiles.clear();
    foreach(QString cliFile, cliFiles) {
      inputFiles.append(QFileInfo(cliFile));
    }
  } 

  if(!config.unattend) {
    std::string userInput = "";
    if(gameListFile.exists() && frontend->canSkip()) {
      printf("\033[1;34mDo you wish to skip existing entries\033[0m (y/N)? ");
      getline(std::cin, userInput);
      if((userInput == "y" || userInput == "Y") && frontend->canSkip()) {
	frontend->skipExisting(gameListFileString, gameEntries, inputFiles);
      }
    }
  }

  totalFiles = inputFiles.length();
  if(totalFiles == 0) {
    // A bit of a hack to let the scraping process take place. We want it to rewrite the gamelist
    printf("No entries to scrape...\n\n");
    doneThreads = config.threads - 1;
    checkThreads();
    exit(0);
  }
  
  printf("\nStarting scraping run on \033[1;32m%d\033[0m files using \033[1;32m%d\033[0m threads.\nSit back, relax and let me do the work! :)\n\n", totalFiles, config.threads);

  timer.start();
  currentFile = 1;

  int filesPerThread = (totalFiles - totalFiles % config.threads) / config.threads;
  if(totalFiles < config.threads) {
    filesPerThread = 1;
  }
  
  if(config.verbose) {
    printf("Files per thread: %d\n", filesPerThread);
  }
  QList<QThread*> threadList;
  for(int curThread = 1; curThread <= config.threads; ++curThread) {
    int beginIdx = (curThread - 1) * filesPerThread;
    if(curThread == config.threads) {
      filesPerThread += totalFiles % config.threads;
    }
    QThread *thread = new QThread;
    ScraperWorker *worker = new ScraperWorker(inputFiles, filesPerThread, beginIdx, config, localDb);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &ScraperWorker::run);
    connect(worker, &ScraperWorker::outputToTerminal, this, &Skyscraper::outputToTerminal);
    connect(worker, &ScraperWorker::entryReady, this, &Skyscraper::entryReady);
    //connect(worker, &ScraperWorker::addToSkipped, this, &Skyscraper::addToSkipped);
    connect(worker, &ScraperWorker::allDone, this, &Skyscraper::checkThreads);
    connect(thread, &QThread::finished, worker, &ScraperWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    threadList.append(thread);
    // Do not start more threads if we have less files than allowed threads
    if(curThread == totalFiles) {
      config.threads = curThread;
      break;
    }
  }
  // Ready, set, GO!!! Start all threads
  foreach(QThread *thread, threadList) {
    thread->start();
  }
}

void Skyscraper::checkForFolder(QDir &folder)
{
  if(!folder.exists()) {
    printf("Folder '%s' doesn't exist, trying to create it... ", folder.absolutePath().toStdString().c_str());
    if(folder.mkpath(folder.absolutePath())) {
      printf("\033[1;32mSuccess!\033[0m\n\n");
    } else {
      printf("\033[1;32mFailed!\033[0m Please check path and permissions, now exiting...\n");
      exit(1);
    }
  }
}

void Skyscraper::outputToTerminal(const QString &output)
{
  outputMutex.lock();

  printf("\033[0;32m#%d/%d\033[0m %s\n", currentFile, totalFiles, output.toStdString().c_str());
  int elapsed = timer.elapsed();
  int estTime = elapsed / currentFile * totalFiles;

  printf("Elapsed time: %s\n", secsToString(elapsed).toStdString().c_str());
  printf("Estimated time: %s\n\n", secsToString(estTime).toStdString().c_str());

  outputMutex.unlock();
}

QString Skyscraper::secsToString(const int &secs)
{
  QString hours = QString::number(secs / 3600000 % 24);
  QString minutes = QString::number(secs / 60000 % 60);
  QString seconds = QString::number(secs / 1000 % 60);
  if(hours.length() == 1) {
    hours.prepend("0");
  }
  if(minutes.length() == 1) {
    minutes.prepend("0");
  }
  if(seconds.length() == 1) {
    seconds.prepend("0");
  }

  return hours + ":" + minutes + ":" + seconds;
}

void Skyscraper::entryReady(const GameEntry &entry)
{
  entryMutex.lock();

  GameEntry tmpEntry = entry;

  if(tmpEntry.found) {
    found++;
    avgCompleteness += tmpEntry.completeness(config.videos);
    avgSearchMatch += tmpEntry.searchMatch;
    // Remove unnecessary media data to save memory before adding it to final entrylist
    // At this point data has been saved to disc, so we don't need it anymore.
    tmpEntry.coverData = QImage();
    tmpEntry.screenshotData = QImage();
    tmpEntry.wheelData = QImage();
    tmpEntry.marqueeData = QImage();
    tmpEntry.videoData = "";
    gameEntries.append(tmpEntry);
  } else {
    notFound++;
    QFile skippedFile(skippedFileString);
    skippedFile.open(QIODevice::Append);
    skippedFile.write("'" + tmpEntry.baseName.toUtf8() + "'");
    if(tmpEntry.searchMatch == 0) {
      skippedFile.write(", No returned matches\n");
    } else {
      skippedFile.write(", Closest match was '" + tmpEntry.title.toUtf8() + "' at " + QByteArray::number(tmpEntry.searchMatch) + "%\n");
    }
    skippedFile.close();
    if(config.skipped) {
      gameEntries.append(tmpEntry);
    }
  }
  
  if(currentFile == 30 && notFound == 30 &&
     config.scraper != "import" && config.scraper != "localdb") {
    printf("\033[1;31mThis is NOT going well! I guit! *slams the door*\nNo, seriously, out of 30 files we had 30 misses. So either the scraping source is down or you are using a scraping source that doesn't support this platform. Please try another scraping module (check '--help').\n\nNow exiting...\033[0m\n");
    exit(1);
  }

  currentFile++;

  entryMutex.unlock();
}

void Skyscraper::checkThreads()
{
  checkThreadMutex.lock();

  doneThreads++;
  if(doneThreads == config.threads) {
    printf("\033[1;34m---- Scraping run completed! YAY! ----\033[0m\n");
    if(!config.dbFolder.isEmpty() && config.localDb) {
      localDb->writeDb();
    }

    frontend->sortEntries(gameEntries);

    QString finalOutput;
    frontend->assembleList(finalOutput, gameEntries, config.maxLength);
    
    if(!config.pretend) {
      QFile gameListFile(gameListFileString);
      printf("Now writing '%s'... ", gameListFileString.toStdString().c_str());
      if(gameListFile.open(QIODevice::WriteOnly)) {
	gameListFile.write(finalOutput.toUtf8());
	gameListFile.close();
	printf("\033[1;32mSuccess!!!\033[0m\n\n");
      } else {
	printf("\033[1;31mCouldn't open file for writing!!!\nAll that work for nothing... :(\033[0m\n");
      }
    }

    printf("\033[1;34m---- And here are some neat stats :) ----\033[0m\n");
    printf("Total completion time: \033[1;33m%s\033[0m\n\n", secsToString(timer.elapsed()).toStdString().c_str());
    if(found > 0) {
      printf("Average search match: \033[1;33m%d%%\033[0m\n",
	     (int)((double)avgSearchMatch / (double)found));
      printf("Average entry completeness: \033[1;33m%d%%\033[0m\n\n",
	     (int)((double)avgCompleteness / (double)found));
    }
    printf("\033[1;34mTotal number of games: %d\033[0m\n", totalFiles);
    printf("\033[1;32mSuccessfully scraped games: %d\033[0m\n", found);
    printf("\033[1;33mSkipped games: %d (Filenames saved to '%s')\033[0m\n\n", notFound, skippedFileString.toStdString().c_str());

    // All done, now clean up and exit to terminal
    emit finished();
  }

  checkThreadMutex.unlock();
}

void Skyscraper::loadConfig(const QCommandLineParser &parser)
{
  QString current;
  QString distro;

  /* -----
     Files that should ALWAYS be updated from distributed default files
     ----- */

  current = "config.ini.example";
  distro = "/usr/local/etc/skyscraper/config.ini.example";
  copyFile(distro, current);

  current = "README.md";
  distro = "/usr/local/etc/skyscraper/README.md";
  copyFile(distro, current);

  current = "ARTWORK.md";
  distro = "/usr/local/etc/skyscraper/ARTWORK.md";
  copyFile(distro, current);

  current = "artwork.xml.example1";
  distro = "/usr/local/etc/skyscraper/artwork.xml.example1";
  copyFile(distro, current);

  current = "artwork.xml.example2";
  distro = "/usr/local/etc/skyscraper/artwork.xml.example2";
  copyFile(distro, current);

  current = "artwork.xml.example3";
  distro = "/usr/local/etc/skyscraper/artwork.xml.example3";
  copyFile(distro, current);

  current = "artwork.xml.example4";
  distro = "/usr/local/etc/skyscraper/artwork.xml.example4";
  copyFile(distro, current);

  current = "mameMap.csv";
  distro = "/usr/local/etc/skyscraper/mameMap.csv";
  copyFile(distro, current);
  
  current = "resources/boxfront.png";
  distro = "/usr/local/etc/skyscraper/resources/boxfront.png";
  copyFile(distro, current);

  current = "resources/boxside.png";
  distro = "/usr/local/etc/skyscraper/resources/boxside.png";
  copyFile(distro, current);

  current = "dbs/README.md";
  distro = "/usr/local/etc/skyscraper/dbs/README.md";
  copyFile(distro, current);

  current = "dbs/priorities.xml.example";
  distro = "/usr/local/etc/skyscraper/dbs/priorities.xml.example";
  copyFile(distro, current);

  current = "import/README.md";
  distro = "/usr/local/etc/skyscraper/import/README.md";
  copyFile(distro, current);

  current = "import/definitions.dat.example1";
  distro = "/usr/local/etc/skyscraper/import/definitions.dat.example1";
  copyFile(distro, current);

  current = "import/definitions.dat.example2";
  distro = "/usr/local/etc/skyscraper/import/definitions.dat.example2";
  copyFile(distro, current);

  /* -----
    Files that will only be overwritten if they don't already exist
    ----- */

  current = "artwork.xml";
  distro = "/usr/local/etc/skyscraper/artwork.xml";
  copyFile(distro, current, false); // False means it won't overwrite if it exists

  current = "resources/maskexample.png";
  distro = "/usr/local/etc/skyscraper/resources/maskexample.png";
  copyFile(distro, current, false);

  current = "resources/frameexample.png";
  distro = "/usr/local/etc/skyscraper/resources/frameexample.png";
  copyFile(distro, current, false);

  // Copy one of the example definitions.dat files if none exists
  current = "import/definitions.dat";
  distro = "/usr/local/etc/skyscraper/import/definitions.dat.example2";
  copyFile(distro, current, false);

  /* -----
     END updating files from distribution files
    ----- */

  QSettings settings(parser.isSet("c")?parser.value("c"):"config.ini", QSettings::IniFormat);

  // Start by setting frontend, since we need it to set default for gamelist and so on
  settings.beginGroup("main");
  if(settings.contains("frontend")) {
    config.frontend = settings.value("frontend").toString();
  }
  settings.endGroup();
  if(parser.isSet("f") && (parser.value("f") == "emulationstation" ||
			   parser.value("f") == "attractmode")) {
    config.frontend = parser.value("f");
  }
  if(config.frontend == "emulationstation") {
    frontend = new EmulationStation;
  } else if(config.frontend == "attractmode") {
    frontend = new AttractMode;
  }

  frontend->setConfig(&config);
  
  bool inputFolderSet = false;
  bool gameListFolderSet = false;
  bool mediaFolderSet = false;
  
  // Main config, overrides defaults
  settings.beginGroup("main");
  if(settings.contains("platform")) {
    config.platform = settings.value("platform").toString();
  }
  if(settings.contains("lang")) {
    config.lang = settings.value("lang").toString();
  }
  if(settings.contains("region")) {
    config.region = settings.value("region").toString();
  }
  if(settings.contains("pretend")) {
    config.pretend = settings.value("pretend").toBool();
  }
  if(settings.contains("unattend")) {
    config.unattend = settings.value("unattend").toBool();
  }
  if(settings.contains("forceFilename")) {
    config.forceFilename = settings.value("forceFilename").toBool();
  }
  if(settings.contains("verbose")) {
    config.verbose = settings.value("verbose").toBool();
  }
  if(settings.contains("maxLength")) {
    config.maxLength = settings.value("maxLength").toInt();
  }
  if(settings.contains("threads")) {
    config.threads = settings.value("threads").toInt();
  }
  if(settings.contains("emulator")) {
    config.emulator = settings.value("emulator").toString();
  }
  if(settings.contains("videos")) {
    config.videos = settings.value("videos").toBool();
  }
  if(settings.contains("skipped")) {
    config.skipped = settings.value("skipped").toBool();
  }
  if(settings.contains("brackets")) {
    config.brackets = !settings.value("brackets").toBool();
  }
  if(settings.contains("artworkXml")) {
    config.artworkConfig = settings.value("artworkXml").toString();
  }
  settings.endGroup();

  settings.beginGroup("localDb");
  if(settings.contains("covers")) {
    config.cacheCovers = settings.value("covers").toBool();
  }
  if(settings.contains("screenshots")) {
    config.cacheScreenshots = settings.value("screenshots").toBool();
  }
  if(settings.contains("wheels")) {
    config.cacheWheels = settings.value("wheels").toBool();
  }
  if(settings.contains("marquees")) {
    config.cacheMarquees = settings.value("marquees").toBool();
  }
  settings.endGroup();
  
  // Check for command line platform here, since we need it for 'platform' config.ini entries
  if(parser.isSet("p") && Platform::getPlatforms().contains(parser.value("p"))) {
    config.platform = parser.value("p");
  } else {
    printf("Please set a valid platform with '-p [platform]'\nCheck '--help' for a list of supported platforms, now exiting...\n");
    exit(1);
  }

  // Platform specific config, overrides main and defaults
  settings.beginGroup(config.platform);
  if(settings.contains("emulator")) {
    config.emulator = settings.value("emulator").toString();
  }
  if(settings.contains("inputFolder")) {
    config.inputFolder = settings.value("inputFolder").toString();
    inputFolderSet = true;
  }
  if(settings.contains("gamelistFolder")) {
    config.gameListFolder = settings.value("gamelistFolder").toString();
    gameListFolderSet = true;
  }
  if(settings.contains("mediaFolder")) {
    config.mediaFolder = settings.value("mediaFolder").toString();;
    mediaFolderSet = true;
  }
  if(settings.contains("skipped")) {
    config.skipped = settings.value("skipped").toBool();
  }
  if(settings.contains("brackets")) {
    config.brackets = !settings.value("brackets").toBool();
  }
  if(settings.contains("minMatch")) {
    config.minMatch = settings.value("minMatch").toInt();
    config.minMatchSet = true;
  }
  if(settings.contains("scraper")) {
    config.scraper = settings.value("scraper").toString();
  }
  if(settings.contains("userCreds")) {
    config.userCreds = settings.value("userCreds").toString();
  }
  if(settings.contains("threads")) {
    config.threads = settings.value("threads").toInt();
  }
  if(settings.contains("pretend")) {
    config.pretend = settings.value("pretend").toBool();
  }
  if(settings.contains("unattend")) {
    config.unattend = settings.value("unattend").toBool();
  }
  if(settings.contains("forceFilename")) {
    config.forceFilename = settings.value("forceFilename").toBool();
  }
  if(settings.contains("verbose")) {
    config.verbose = settings.value("verbose").toBool();
  }
  if(settings.contains("maxLength")) {
    config.maxLength = settings.value("maxLength").toInt();
  }
  if(settings.contains("artworkXml")) {
    config.artworkConfig = settings.value("artworkXml").toString();
  }
  settings.endGroup();
  
  // Command line configs, overrides platform, main and defaults
  if(parser.isSet("l") && parser.value("l").toInt() >= 0 && parser.value("l").toInt() <= 10000) {
    config.maxLength = parser.value("l").toInt();
  }
  if(parser.isSet("t") && parser.value("t").toInt() <= 8) {
    config.threads = parser.value("t").toInt();
  }
  if(parser.isSet("e")) {
    config.emulator = parser.value("e");
  }
  if(parser.isSet("i")) {
    config.inputFolder = parser.value("i");
    inputFolderSet = true;
  }
  if(parser.isSet("g")) {
    config.gameListFolder = parser.value("g");
    gameListFolderSet = true;
  }
  if(parser.isSet("o")) {
    config.mediaFolder = parser.value("o");
    mediaFolderSet = true;
  }
  if(parser.isSet("a")) {
    config.artworkConfig = parser.value("a");
  }
  if(parser.isSet("m") && parser.value("m").toInt() >= 0 && parser.value("m").toInt() <= 100) {
    config.minMatch = parser.value("m").toInt();
    config.minMatchSet = true;
  }
  if(parser.isSet("s") && (parser.value("s") == "openretro" ||
			   parser.value("s") == "thegamesdb" ||
			   parser.value("s") == "arcadedb" ||
			   parser.value("s") == "worldofspectrum" ||
			   parser.value("s") == "screenscraper" ||
			   parser.value("s") == "localdb" ||
			   parser.value("s") == "import")) {
    config.scraper = parser.value("s");
  }
  if(parser.isSet("u") && parser.value("u").indexOf(":") != -1) {
    config.userCreds = parser.value("u");
  }
  if(parser.isSet("d")) {
    config.dbFolder = parser.value("d");
  } else {
    config.dbFolder = "dbs/" + config.platform;
  }
  if(parser.isSet("videos")) {
    config.videos = true;
  }
  if(parser.isSet("nocovers")) {
    config.cacheCovers = false;
  }
  if(parser.isSet("noscreenshots")) {
    config.cacheScreenshots = false;
  }
  if(parser.isSet("nowheels")) {
    config.cacheWheels = false;
  }
  if(parser.isSet("nomarquees")) {
    config.cacheMarquees = false;
  }
  if(parser.isSet("skipped")) {
    config.skipped = true;
  }
  if(parser.isSet("nobrackets")) {
    config.brackets = false;
  }
  if(parser.isSet("nolocaldb")) {
    if(config.scraper == "localdb") {
      printf("Selected scraper 'localdb' can't be used with '--nolocaldb' flag, exiting.\n");
      exit(1);
    }
    config.localDb = false;
  }
  if(parser.isSet("cleandb")) {
    config.cleanDb = true;
  }
  if(parser.isSet("mergedb") && QDir(config.mergeDb).exists()) {
    config.mergeDb = parser.value("mergedb");
  }
  if(parser.isSet("updatedb")) {
    config.updateDb = true;
  }
  if(parser.isSet("noresize")) {
    config.noResize = true;
  }
  if(parser.isSet("nosubdirs")) {
    config.subDirs = false;
  }
  if(parser.isSet("pretend")) {
    config.pretend = true;
  }
  if(parser.isSet("unattend")) {
    config.unattend = true;
  }
  if(parser.isSet("forcefilename")) {
    config.forceFilename = true;
  }
  if(parser.isSet("region")) {
    config.region = parser.value("region");
  }
  if(parser.isSet("lang")) {
    config.lang = parser.value("lang");
  }
  if(parser.isSet("verbose")) {
    config.verbose = true;
  }
  if(parser.isSet("l") && parser.value("l").toInt() >= 0 && parser.value("l").toInt() <= 10000) {
    config.maxLength = parser.value("l").toInt();
  }

  frontend->checkReqs();

  // Change config defaults if they aren't already set, find the rest in settings.h
  if(!inputFolderSet) {
    config.inputFolder = frontend->getInputFolder();
  }
  if(!gameListFolderSet) {
    config.gameListFolder = frontend->getGameListFolder();
  }
  if(!mediaFolderSet) {
    config.mediaFolder = config.gameListFolder + "/media";
  }
  config.coversFolder = frontend->getCoversFolder();
  config.screenshotsFolder = frontend->getScreenshotsFolder();
  config.wheelsFolder = frontend->getWheelsFolder();
  config.marqueesFolder = frontend->getMarqueesFolder();
  config.videosFolder = frontend->getVideosFolder();
  
  // Choose default scraper for chosen platform if none has been set yet
  if(config.scraper.isEmpty()) {
    config.scraper = Platform::getDefaultScraper(config.platform);
  }

  // Set minMatch to 0 for localdb, arcadedb and screenscraper
  // We know these results are always accurate
  if(config.minMatchSet == false && (config.scraper == "localdb" ||
				     config.scraper == "screenscraper" ||
				     config.scraper == "arcadedb")) {
    config.minMatch = 0;
  }

  skippedFileString = "skipped-" + config.scraper + ".txt";

  // If user has set specific files to scrape on command line set them internally
  foreach(QString cliArgument, parser.positionalArguments()) {
    bool exists = false;
    if(QFile::exists(cliArgument)) {
      exists = true;
    } else if(QFile::exists(cliArgument.prepend(config.currentDir + "/"))) {
      exists = true;
    }
    if(exists) {
      cliFiles.append(cliArgument);
      // Always set pretend, updateDb and unattend true if user has supplied filenames on
      // command line. That way they are cached, but game list is not changed and user isn't
      // asked about skipping and overwriting.
      config.pretend = true;
      config.updateDb = true;
      config.unattend = true;
    }
  }

  if(config.scraper == "import") {
    // Always force local db to be updated when using import scraper
    config.updateDb = true;
  }

  QFile artworkFile(config.artworkConfig);
  if(artworkFile.open(QIODevice::ReadOnly)) {
    config.artworkXml = artworkFile.readAll();
    artworkFile.close();
  } else {
    printf("Couldn't read artwork xml file '\033[1;32m%s\033[0m'. Please check file and permissions. Now exiting...\n", config.artworkConfig.toStdString().c_str());
    exit(1);
  }
  
  QDir resDir("./resources");
  QDirIterator resDirIt(resDir.absolutePath(),
			QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
			QDirIterator::Subdirectories);
  while(resDirIt.hasNext()) {
    QString resFile = resDirIt.next();
    resFile = resFile.remove(0, resFile.indexOf("resources/") + 10); // Also cut off 'resources/'
    config.resources[resFile] = QImage("resources/" + resFile);
  }
}

void Skyscraper::copyFile(QString &distro, QString &current, bool overwrite)
{
  if(QFileInfo::exists(distro)) {
    if(QFileInfo::exists(current)) {
      if(overwrite) {
	QFile::remove(current);
	QFile::copy(distro, current);
      }
    } else {
      QFile::copy(distro, current);
    }
  }
}

// --- Console colors ---
// Black        0;30     Dark Gray     1;30
// Red          0;31     Light Red     1;31
// Green        0;32     Light Green   1;32
// Brown/Orange 0;33     Yellow        1;33
// Blue         0;34     Light Blue    1;34
// Purple       0;35     Light Purple  1;35
// Cyan         0;36     Light Cyan    1;36
// Light Gray   0;37     White         1;37
