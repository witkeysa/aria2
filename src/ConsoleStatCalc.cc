/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "ConsoleStatCalc.h"
#include "RequestGroupMan.h"
#include "RequestGroup.h"
#include "FileAllocationMan.h"
#include "FileAllocationEntry.h"
#include "CheckIntegrityMan.h"
#include "CheckIntegrityEntry.h"
#include "Util.h"
#ifdef ENABLE_BITTORRENT
# include "BtContext.h"
#endif // ENABLE_BITTORRENT
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iterator>

namespace aria2 {

static void printProgress(std::ostream& o, const SharedHandle<RequestGroup>& rg)
{
  TransferStat stat = rg->calculateStat();
  unsigned int eta = 0;
  if(rg->getTotalLength() > 0 && stat.getDownloadSpeed() > 0) {
    eta = (rg->getTotalLength()-rg->getCompletedLength())/stat.getDownloadSpeed();
  }

  o << "["
    << "#" << rg->getGID() << " ";
#ifdef ENABLE_BITTORRENT
  if(rg->downloadFinished() &&
     !dynamic_pointer_cast<BtContext>(rg->getDownloadContext()).isNull()) {
    o << "SEEDING" << "(" << "ratio:"
      << std::fixed << std::setprecision(1)
      << ((stat.getAllTimeUploadLength()*10)/rg->getCompletedLength())/10.0
      << ")";
  } else
#endif // ENABLE_BITTORRENT
    {
      o << "SIZE:"
	<< Util::abbrevSize(rg->getCompletedLength())
	<< "B"
	<< "/"
	<< Util::abbrevSize(rg->getTotalLength())
	<< "B";
      if(rg->getTotalLength() > 0) {
	o << "("
	  << 100*rg->getCompletedLength()/rg->getTotalLength()
	  << "%)";
      }
    }
  o << " "
    << "CN:"
    << rg->getNumConnection();
  if(!rg->downloadFinished()) {
    o << " "
      << "SPD:"
      << std::fixed << std::setprecision(2) << stat.getDownloadSpeed()/1024.0 << "KiB/s";
  }
  if(stat.getSessionUploadLength() > 0) {
    o << " "
      << "UP:"
      << std::fixed << std::setprecision(2) << stat.getUploadSpeed()/1024.0 << "KiB/s"
      << "(" << Util::abbrevSize(stat.getAllTimeUploadLength()) << "B)";
  }
  if(eta > 0) {
    o << " "
      << "ETA:"
      << Util::secfmt(eta);
  }
  o << "]";
}

class PrintSummary
{
private:
  size_t _cols;
public:
  PrintSummary(size_t cols):_cols(cols) {}

  void operator()(const SharedHandle<RequestGroup>& rg)
  {
    const char SEP_CHAR = '-';
    printProgress(std::cout, rg);
    std::cout << "\n"
	      << "FILE: " << rg->getFilePath() << "\n"
	      << std::setfill(SEP_CHAR) << std::setw(_cols) << SEP_CHAR << "\n";
  }
};

static void printProgressSummary(const std::deque<SharedHandle<RequestGroup> >& groups, size_t cols)
{
  const char SEP_CHAR = '=';
  time_t now;
  time(&now);
  std::cout << " *** Download Progress Summary";
  {
    time_t now;
    struct tm* staticNowtmPtr;
    char buf[26];
    if(time(&now) != (time_t)-1 && (staticNowtmPtr = localtime(&now)) != 0 &&
       asctime_r(staticNowtmPtr, buf) != 0) {
      char* lfptr = strchr(buf, '\n');
      if(lfptr) {
	*lfptr = '\0';
      }
      std::cout << " as of " << buf;
    }
  }
  std::cout << " *** " << "\n"
	    << std::setfill(SEP_CHAR) << std::setw(cols) << SEP_CHAR << "\n";
  std::for_each(groups.begin(), groups.end(), PrintSummary(cols));
}

ConsoleStatCalc::ConsoleStatCalc(time_t summaryInterval):
  _summaryInterval(summaryInterval),
  _summaryIntervalCount(0)
{}

void
ConsoleStatCalc::calculateStat(const RequestGroupManHandle& requestGroupMan,
			       const FileAllocationManHandle& fileAllocationMan,
			       const CheckIntegrityManHandle& checkIntegrityMan)
{
  if(!_cp.elapsed(1)) {
    return;
  }
  _cp.reset();
  ++_summaryIntervalCount;

  bool isTTY = isatty(STDOUT_FILENO) == 1;
  unsigned short int cols = 80;
  if(isTTY) {
    struct winsize size;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
      cols = size.ws_col;
    }
    std::cout << '\r' << std::setfill(' ') << std::setw(cols) << ' ' << '\r';
  }
  std::ostringstream o;
  if(requestGroupMan->countRequestGroup() > 0) {
    if(_summaryInterval > 0 && _summaryIntervalCount%_summaryInterval == 0) {
      printProgressSummary(requestGroupMan->getRequestGroups(), cols);
      _summaryIntervalCount = 0;
      std::cout << "\n";
    }

    RequestGroupHandle firstRequestGroup = requestGroupMan->getRequestGroup(0);

    printProgress(o, firstRequestGroup);

    if(requestGroupMan->countRequestGroup() > 1) {
      o << "("
	<< requestGroupMan->countRequestGroup()-1
	<< "more...)";
    }
  }

  if(requestGroupMan->countRequestGroup() > 1 &&
     !requestGroupMan->downloadFinished()) {
    TransferStat stat = requestGroupMan->calculateStat();
    o << " "
      << "[TOTAL SPD:"
      << std::fixed << std::setprecision(2) << stat.getDownloadSpeed()/1024.0 << "KiB/s" << "]";
  }

  {
    FileAllocationEntryHandle entry = fileAllocationMan->getCurrentFileAllocationEntry();
    if(!entry.isNull()) {
      o << " "
	<< "[FileAlloc:"
	<< "#" << entry->getRequestGroup()->getGID() << " "
	<< Util::abbrevSize(entry->getCurrentLength())
	<< "B"
	<< "/"
	<< Util::abbrevSize(entry->getTotalLength())
	<< "B"
	<< "(";
      if(entry->getTotalLength() > 0) {
	o << 100*entry->getCurrentLength()/entry->getTotalLength();
      } else {
	o << "--";
      }
      o << "%)"
	<< "]";
      if(fileAllocationMan->countFileAllocationEntryInQueue() > 0) {
	o << "("
	  << fileAllocationMan->countFileAllocationEntryInQueue()
	  << "waiting...)";
      }
    }
  }
#ifdef ENABLE_MESSAGE_DIGEST
  {
    CheckIntegrityEntryHandle entry = checkIntegrityMan->getFirstCheckIntegrityEntry();
    if(!entry.isNull()) {
      o << " "
	<< "[Checksum:"
	<< "#" << entry->getRequestGroup()->getGID() << " "
	<< Util::abbrevSize(entry->getCurrentLength())
	<< "B"
	<< "/"
	<< Util::abbrevSize(entry->getTotalLength())
	<< "B"
	<< "("
	<< 100*entry->getCurrentLength()/entry->getTotalLength()
	<< "%)"
	<< "]";
      if(checkIntegrityMan->countCheckIntegrityEntry() > 1) {
	o << "("
	  << checkIntegrityMan->countCheckIntegrityEntry()-1
	  << "more...)";
      }
    }
  }
#endif // ENABLE_MESSAGE_DIGEST
  std::string readout = o.str();
  if(isTTY) {
    std::string::iterator last = readout.begin();
    if(readout.size() > cols) {
      std::advance(last, cols);
    } else {
      last = readout.end();
    }
    std::copy(readout.begin(), last, std::ostream_iterator<char>(std::cout));
    std::cout << std::flush;
  } else {
    std::copy(readout.begin(), readout.end(), std::ostream_iterator<char>(std::cout));
    std::cout << std::endl;
  }
}

} // namespace aria2
