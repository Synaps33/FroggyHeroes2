/***************************************************************************
 *   Copyright (C) 2009 by Andrey Afletdinov <fheroes2@gmail.com>          *
 *                                                                         *
 *   Part of the Free Heroes2 Engine:                                      *
 *   http://sourceforge.net/projects/fheroes2                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifdef SF2000
// Vendored copy of the multicore frontend's dirent.h, under a distinct name so
// it does not shadow the system <dirent.h> on host builds (src/fheroes2/system
// is on the include path).
#include "sf2000_dirent.h"
#else
#include <dirent.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include "gamedefs.h"
#include "settings.h"
#include "dir.h"

Dir::Dir()
{
}

void Dir::Read(const std::string &path, const std::string &filter, bool sensitive)
{
    // read directory
    DIR *dp;
    struct dirent *ep;
#ifndef SF2000
    struct stat fs;
#endif

    dp = opendir(path.c_str());

    DEBUG(DBG_ENGINE , DBG_INFO, "Dir::Read: " << (filter.size() ? path + " (" + filter + ")" : path));

    if(dp)
    {
	while(NULL != (ep = readdir(dp)))
	{
	    const std::string fullname(path + SEPARATOR + ep->d_name);

    	    // if not regular file
#ifdef SF2000
    	    if(!DIRENT_ISFILE(ep->d_type)) continue;
#else
    	    if(stat(fullname.c_str(), &fs) || !S_ISREG(fs.st_mode)) continue;
#endif

	    if(filter.size())
	    {
    		std::string filename(ep->d_name);

		if(sensitive)
		{
		    if(std::string::npos == filename.find(filter)) continue;
    		}
    		else
    		{
    		    std::string filterlow(filter);
    		    String::Lower(filterlow);
    		    String::Lower(filename);

		    if(std::string::npos == filename.find(filterlow)) continue;
		}
    	    }

    	    push_back(fullname);
	}
	closedir(dp);
    }
}
