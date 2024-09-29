/****************************************************************************
   Copyright (C) 2005-2023, rncbc aka Rui Nuno Capela. All rights reserved.
   Copyright (C) 2024- Stazed

   This file is part of Non-Mixer-XT

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*****************************************************************************/

/*
 * File:   VST2_Preset.H
 * Author: sspresto
 *
 * Created on January 17, 2024, 7:19 AM
 */

#pragma once

#ifdef VST2_SUPPORT

#include <fstream>

class VST2_Plugin;

class VST2_Preset
{
public:

    // Constructor.
    VST2_Preset(VST2_Plugin *pVst2Plugin);

    // File loader/saver.
    bool load(const std::string& sFilename);
    bool save(const std::string& sFilename);

    bool get_program_names(std::vector<std::string>  &v_program_names);

protected:

    // Forward decls.
    struct BaseHeader;
    struct BankHeader;
    struct ProgHeader;
    struct Chunk;

    // Loader methods.
//    bool load_bank_progs(QFile& file);
    bool load_bank_progs(FILE& file);
    bool load_prog_params(FILE& file);
    bool load_bank_chunk(FILE& file);
    bool load_prog_chunk(FILE& file);
    bool load_chunk(FILE& file, int preset);

    // Saver methods.
    bool save_bank_progs(FILE& file);
    bool save_prog_params(FILE& file);
    bool save_bank_chunk(FILE& file, const Chunk& chunk);
    bool save_prog_chunk(FILE& file, const Chunk& chunk);
    bool save_chunk(FILE& file, const Chunk& chunk);
    bool get_chunk(Chunk& chunk, int preset);

private:

    // Instance variables.
    VST2_Plugin *m_pVst2Plugin;
};


#endif // VST2_SUPPORT

