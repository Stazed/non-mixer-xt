/****************************************************************************
   Copyright (C) 2005-2023, rncbc aka Rui Nuno Capela. All rights reserved.

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
 * File:   VST2_Preset.C
 * Author: sspresto
 * 
 * Created on January 17, 2024, 7:19 AM
 */

#ifdef VST2_SUPPORT

#include <filesystem> // C++17
#include "VST2_Preset.H"
#include "VST2_Plugin.H"

//-----------------------------------------------------------------------------
// Structures for VST2 presets (fxb/fxp files)

// Constants copied/stringified from "vstfxstore.h"
//
#define cMagic				"CcnK"		// Root chunk identifier for Programs (fxp) and Banks (fxb).
#define fMagic				"FxCk"		// Regular Program (fxp) identifier.
#define bankMagic			"FxBk"		// Regular Bank (fxb) identifier.
#define chunkPresetMagic	"FPCh"		// Program (fxp) identifier for opaque chunk data.
#define chunkBankMagic		"FBCh"		// Bank (fxb) identifier for opaque chunk data.

#define cMagic_u			0x4b6e6343  // Big-endian version of 'CcnK'

typedef int32_t  VstInt32;

// Some VeSTige missing opcodes and flags.
const int effSetProgramName = 4;
const int effGetChunk = 23;
const int effSetChunk = 24;


// Common bank/program header structure (fxb/fxp files)
//
struct VST2_Preset::BaseHeader
{
    VstInt32 chunkMagic;		// 'CcnK'
    VstInt32 byteSize;			// size of this chunk, excl. magic + byteSize
    VstInt32 fxMagic;			// 'FxCk' (regular) or 'FPCh' (opaque chunk)
    VstInt32 version;			// format version (currently 1)
    VstInt32 fxID;				// fx unique ID
    VstInt32 fxVersion;			// fx version
};


// Program sub-header structure (fxb/fxp files)
//
struct VST2_Preset::ProgHeader
{
    VstInt32 numParams;			// number of parameters
    char prgName[28];			// program name (null-terminated ASCII string)
};


// Bank sub-header structure (fxb files)
//
struct VST2_Preset::BankHeader
{
    VstInt32 numPrograms;		// number of programs
    VstInt32 currentProgram;	// version 2: current program number
    char future[124];			// reserved, should be zero
};


// Common auxiliary chunk structure (chunked fxb/fxp files)
//
struct VST2_Preset::Chunk
{
    VstInt32 size;
    char *data;
};


// Endianess swappers.
//
static inline void fx_endian_swap ( VstInt32& v )
{
    static bool s_endian_swap = (*(VstInt32 *) cMagic == cMagic_u);

    if (s_endian_swap) {
            const unsigned int u = v;
            v = (u << 24) | ((u << 8) & 0xff0000) | ((u >> 8) & 0xff00) | (u >> 24);
    }
}

static inline void fx_endian_swap ( float& v )
{
    fx_endian_swap(*(VstInt32 *) &v);
}


static inline bool fx_is_magic ( VstInt32 v, const char *c )
{
#if 0
    VstInt32 u = *(VstInt32 *) c;
    fx_endian_swap(u);
    return (v == u);
#else
    return (v == *(VstInt32 *) c);
#endif
}


//----------------------------------------------------------------------
// class VST2_Preset -- VST2 preset file interface
//

// Constructor.
VST2_Preset::VST2_Preset ( VST2_Plugin *pVst2Plugin )
	: m_pVst2Plugin(pVst2Plugin)
{
}


// Loader methods.
//
bool VST2_Preset::load_bank_progs ( FILE& file )
{
    BankHeader bank_header;
    const int nread_bank = sizeof(bank_header);
    
    if (fread((char *) &bank_header, nread_bank, 1, &file) < 1)
        return false;

    fx_endian_swap(bank_header.numPrograms);
    fx_endian_swap(bank_header.currentProgram);

    const int iNumPrograms = int(bank_header.numPrograms);
    const int iCurrentProgram
            = m_pVst2Plugin->vst2_dispatch(effGetProgram, 0, 0, nullptr, 0.0f);

    for (int iProgram = 0; iProgram < iNumPrograms; ++iProgram)
    {
        BaseHeader base_header;
        const int nread = sizeof(base_header);
        
        if (fread((char *) &base_header, nread, 1, &file) < 1)
            return false;

//	fx_endian_swap(base_header.chunkMagic);
        fx_endian_swap(base_header.byteSize);
//	fxendian_swap(base_header.fxMagic);
        fx_endian_swap(base_header.version);
        fx_endian_swap(base_header.fxID);
        fx_endian_swap(base_header.fxVersion);

        if (!fx_is_magic(base_header.chunkMagic, cMagic))
            return false;

        m_pVst2Plugin->vst2_dispatch(effSetProgram, 0, iProgram, nullptr, 0.0f);

        if (fx_is_magic(base_header.fxMagic, fMagic))
        {
            if (!load_prog_params(file))
                return false;
        }
        else
        if (fx_is_magic(base_header.fxMagic, chunkPresetMagic))
        {
            if (!load_prog_chunk(file))
                return false;
        }
        else return false;
    }

    m_pVst2Plugin->vst2_dispatch(effSetProgram, 0, iCurrentProgram, nullptr, 0.0f);

    return true;
}


bool VST2_Preset::load_prog_params ( FILE& file )
{
    ProgHeader prog_header;
    const int nread = sizeof(prog_header);

    if (fread((char *) &prog_header, nread, 1, &file) < 1)
        return false;

    fx_endian_swap(prog_header.numParams);

    m_pVst2Plugin->vst2_dispatch(effSetProgramName, 0, 0, (void *) prog_header.prgName, 0.0f);

    const int iNumParams = int(prog_header.numParams);
    if (iNumParams < 1)
            return false;

    const int nread_params = iNumParams * sizeof(float);
    float *params = new float [iNumParams];
    
    if (fread((char *) params, nread_params, 1, &file) < 1)
        return false;

    for (int iParam = 0; iParam < iNumParams; ++iParam)
    {
        fx_endian_swap(params[iParam]);

        AEffect *pVst2Effect = m_pVst2Plugin->vst2_effect();
        if (pVst2Effect)
            pVst2Effect->setParameter(pVst2Effect, iParam, params[iParam]);
    }

    delete [] params;
    return true;
}


bool VST2_Preset::load_bank_chunk ( FILE& file )
{
    BankHeader bank_header;
    const int nread = sizeof(bank_header);

    if (fread((char *) &bank_header, nread, 1, &file) < 1)
        return false;

    const int iCurrentProgram
        = m_pVst2Plugin->vst2_dispatch(effGetProgram, 0, 0, nullptr, 0.0f);

    const bool bResult = load_chunk(file, 0);

    m_pVst2Plugin->vst2_dispatch(effSetProgram, 0, iCurrentProgram, nullptr, 0.0f);

    return bResult;
}


bool VST2_Preset::load_prog_chunk ( FILE& file )
{
    ProgHeader prog_header;
    const int nread = sizeof(prog_header);

    if( fread((char *) &prog_header, nread, 1, &file) < 1)
        return false;

    return load_chunk(file, 1);
}


bool VST2_Preset::load_chunk ( FILE& file, int preset )
{
    Chunk chunk;
    const int nread = sizeof(chunk.size);

    if (fread((char *) &chunk.size, nread, 1, &file) < 1)
        return false;

    fx_endian_swap(chunk.size);

    const int ndata = int(chunk.size);
    chunk.data = new char [ndata];

    if (fread(chunk.data, ndata, 1, &file) < 1)
    {
        delete [] chunk.data;
        return false;
    }

    m_pVst2Plugin->vst2_dispatch(effSetChunk,
            preset, chunk.size,	(void *) chunk.data, 0.0f);

    delete [] chunk.data;
    return true;
}


// File loader.
//
bool VST2_Preset::load ( const std::string& sFilename )
{
    if (m_pVst2Plugin == nullptr)
        return false;

//    std::filesystem::path filePath = sFilename;
//    const std::string& sExt = filePath.extension();
//    const bool bFxBank = (sExt == ".fxb");
//    const bool bFxProg = (sExt == ".fxp");

    // Hard coded to bFxProg since that is what is hard code saved.
    const bool bFxBank = false;
    const bool bFxProg = true;

    if (!bFxBank && !bFxProg)
        return false;

    FILE *fp = NULL;
    fp = fopen(sFilename.c_str(), "r");

    if (fp == NULL)
    {
       // fl_alert( "Cannot open file %s", sFilename.c_str());
        return false;
    }

    BaseHeader base_header;
    const int nread_base = sizeof(base_header);
    
    if (fread((char *) &base_header, nread_base, 1, fp) < 1)
    {
        DMESSAGE("Cant read base header");
        fclose(fp);
        return false;
    }

    DMESSAGE("VST2_Preset::load(\"%s\")", sFilename.c_str());

//	fx_endian_swap(base_header.chunkMagic);
    fx_endian_swap(base_header.byteSize);
//	fx_endian_swap(base_header.fxMagic);
    fx_endian_swap(base_header.version);
    fx_endian_swap(base_header.fxID);
    fx_endian_swap(base_header.fxVersion);

    bool bResult = false;

    if (!fx_is_magic(base_header.chunkMagic, cMagic))
    {
        DMESSAGE("VST2_Presetload() header.chunkMagic is not \"%s\".", cMagic);
    }
    else
    if (base_header.fxID != VstInt32(m_pVst2Plugin->get_unique_id()))
    {
        DMESSAGE("VST2_Preset::load() header.fxID != 0x%08lx.", m_pVst2Plugin->get_unique_id());
    }
    else
    if (fx_is_magic(base_header.fxMagic, bankMagic))
    {
        DMESSAGE("VST2_Preset::load() header.fxMagic is \"%s\" (regular fxb)", bankMagic);
        bResult = load_bank_progs(*fp);
    }
    else
    if (fx_is_magic(base_header.fxMagic, chunkBankMagic))
    {
        DMESSAGE("VST2_Preset::load() header.fxMagic is \"%s\" (chunked fxb)", chunkBankMagic);
        bResult = load_bank_chunk(*fp);
    }
    else
    if (fx_is_magic(base_header.fxMagic, fMagic))
    {
        DMESSAGE("VST2_Preset::load() header.fxMagic is \"%s\" (regular fxp)", fMagic);
        bResult = load_prog_params(*fp);
    }
    else
    if (fx_is_magic(base_header.fxMagic, chunkPresetMagic))
    {
        DMESSAGE("VST2_Preset::load() header.fxMagic is \"%s\" (chunked fxp)", chunkPresetMagic);
        bResult = load_prog_chunk(*fp);
    }
    else DMESSAGE("VST2_Preset::load() header.fxMagic not recognized.");

    fclose(fp);

    // HACK: Make sure all displayed parameter values are in sync.
    m_pVst2Plugin->updateParamValues(false);

    return bResult;
}


// Saver methods.
//
bool VST2_Preset::save_bank_progs ( FILE& file )
{
    if (m_pVst2Plugin == nullptr)
            return false;

    AEffect *pVst2Effect = m_pVst2Plugin->vst2_effect();
    if (pVst2Effect == nullptr)
            return false;

    const int iNumPrograms = pVst2Effect->numPrograms;
    if (iNumPrograms < 1)
            return false;

    const int iCurrentProgram
            = m_pVst2Plugin->vst2_dispatch(effGetProgram, 0, 0, nullptr, 0.0f);
    const int iVst2Version
            = m_pVst2Plugin->vst2_dispatch(effGetVstVersion, 0, 0, nullptr, 0.0f);

    BankHeader bank_header;
    ::memset(&bank_header, 0, sizeof(bank_header));
    bank_header.numPrograms = iNumPrograms;
    bank_header.currentProgram = iCurrentProgram;

    fx_endian_swap(bank_header.numPrograms);
    fx_endian_swap(bank_header.currentProgram);

    fwrite((char *) &bank_header, sizeof(bank_header), 1, &file);

    const bool bChunked = m_pVst2Plugin->isConfigure();

    bool bResult = false;

    for (int iProgram = 0; iProgram < iNumPrograms; ++iProgram) {

            m_pVst2Plugin->vst2_dispatch(effSetProgram, 0, iProgram, nullptr, 0.0f);

            BaseHeader base_header;
            ::memset(&base_header, 0, sizeof(base_header));
            base_header.chunkMagic = *(VstInt32 *) cMagic;
            base_header.byteSize = 0; // FIXME!
            base_header.fxMagic = *(VstInt32 *) (bChunked ? chunkPresetMagic : fMagic);
            base_header.version = 1;
            base_header.fxID = m_pVst2Plugin->get_unique_id();
            base_header.fxVersion = iVst2Version;

            // Estimate size of this section...
            base_header.byteSize = sizeof(base_header)
                    - sizeof(base_header.chunkMagic)
                    - sizeof(base_header.byteSize);

            Chunk chunk;
            if (bChunked) {
                    get_chunk(chunk, 1);
                    base_header.byteSize += sizeof(chunk.size) + chunk.size;
            } else {
                    const int iNumParams  = pVst2Effect->numParams;
                    base_header.byteSize += sizeof(ProgHeader);
                    base_header.byteSize += iNumParams * sizeof(float);
            }

    //	fx_endian_swap(base_header.chunkMagic);
            fx_endian_swap(base_header.byteSize);
    //	fx_endian_swap(base_header.fxMagic);
            fx_endian_swap(base_header.version);
            fx_endian_swap(base_header.fxID);
            fx_endian_swap(base_header.fxVersion);

            fwrite((char *) &base_header, sizeof(base_header), 1, &file);

            if (bChunked) {
                    bResult = save_prog_chunk(file, chunk);
                    if (!bResult) break;
            } else {
                    bResult = save_prog_params(file);
                    if (!bResult) break;
            }
    }

    m_pVst2Plugin->vst2_dispatch(effSetProgram, 0, iCurrentProgram, nullptr, 0.0f);

    return bResult;
}


bool VST2_Preset::save_prog_params ( FILE& file )
{
    if (m_pVst2Plugin == nullptr)
            return false;

    AEffect *pVst2Effect = m_pVst2Plugin->vst2_effect();
    if (pVst2Effect == nullptr)
            return false;

    const int iNumParams = pVst2Effect->numParams;
    if (iNumParams < 1)
            return false;

    ProgHeader prog_header;
    ::memset(&prog_header, 0, sizeof(prog_header));
    prog_header.numParams = iNumParams;

    m_pVst2Plugin->vst2_dispatch(effGetProgramName, 0, 0, (void *) prog_header.prgName, 0.0f);

    fx_endian_swap(prog_header.numParams);

    fwrite((char *) &prog_header, sizeof(prog_header), 1, &file);

    float *params = new float [iNumParams];
    for (int iParam = 0; iParam < iNumParams; ++iParam) {
            params[iParam] = pVst2Effect->getParameter(pVst2Effect, iParam);
            fx_endian_swap(params[iParam]);
    }

    fwrite((char *) params, iNumParams * sizeof(float), 1, &file);
    delete [] params;

    return true;
}


bool VST2_Preset::save_bank_chunk ( FILE& file, const Chunk& chunk )
{
    if (m_pVst2Plugin == nullptr)
            return false;

    AEffect *pVst2Effect = m_pVst2Plugin->vst2_effect();
    if (pVst2Effect == nullptr)
            return false;

    const int iNumPrograms = pVst2Effect->numPrograms;
    const int iCurrentProgram
            = m_pVst2Plugin->vst2_dispatch(effGetProgram, 0, 0, nullptr, 0.0f);

    BankHeader bank_header;
    ::memset(&bank_header, 0, sizeof(bank_header));
    bank_header.numPrograms = iNumPrograms;
    bank_header.currentProgram = iCurrentProgram;

    fx_endian_swap(bank_header.numPrograms);
    fx_endian_swap(bank_header.currentProgram);

    fwrite((char *) &bank_header, sizeof(bank_header), 1, &file);

    return save_chunk(file, chunk);
}


bool VST2_Preset::save_prog_chunk ( FILE& file, const Chunk& chunk )
{
    if (m_pVst2Plugin == nullptr)
            return false;

    AEffect *pVst2Effect = m_pVst2Plugin->vst2_effect();
    if (pVst2Effect == nullptr)
            return false;

    const int iNumParams = pVst2Effect->numParams;

    ProgHeader prog_header;
    ::memset(&prog_header, 0, sizeof(prog_header));
    prog_header.numParams = iNumParams;

    m_pVst2Plugin->vst2_dispatch(effGetProgramName,
            0, 0, (void *) prog_header.prgName, 0.0f);

    fx_endian_swap(prog_header.numParams);

    fwrite((char *) &prog_header, sizeof(prog_header), 1, &file);

    return save_chunk(file, chunk);
}


bool VST2_Preset::save_chunk ( FILE& file, const Chunk& chunk )
{
    const int ndata = int(chunk.size);

    VstInt32 chunk_size = ndata;
    fx_endian_swap(chunk_size);

    fwrite((char *) &chunk_size, sizeof(chunk_size), 1, &file);
    fwrite((char *)  chunk.data, ndata, 1, &file);

    return true;
}


bool VST2_Preset::get_chunk ( Chunk& chunk, int preset )
{
    chunk.data = nullptr;
    chunk.size = m_pVst2Plugin->vst2_dispatch(
        effGetChunk, preset, 0, (void *) &chunk.data, 0.0f);

    return (chunk.size > 0 && chunk.data != nullptr);
}


// File saver.
//
bool VST2_Preset::save ( const std::string& sFilename )
{
    if (m_pVst2Plugin == nullptr)
            return false;

    AEffect *pVst2Effect = m_pVst2Plugin->vst2_effect();
    if (pVst2Effect == nullptr)
            return false;
    
    std::filesystem::path filePath(sFilename);
    const std::string& sExt = filePath.extension();

//    const bool bFxBank = (sExt == ".fxb");
//    const bool bFxProg = (sExt == ".fxp");

    // We are hard coding this to bFxProg since we have to pick one.
    // Not sure which is best... It looks like bFxProg is the early
    // version and bFxBank is VST_2_4_EXTENSIONS. Just guessing that
    // using the early version might apply to more plugins?? It seems
    // to work for every thing so far...
    const bool bFxBank = false;
    const bool bFxProg = true;

    if (!bFxBank && !bFxProg)
        return false;

    FILE *fp = NULL;
    fp = fopen(sFilename.c_str(), "w");

    if (fp == NULL)
    {
       // fl_alert( "Cannot open file %s", sFilename.c_str());
        return false;
    }

    DMESSAGE("VST2_Preset::save(\"%s\")", sFilename.c_str());

    const bool bChunked
            = m_pVst2Plugin->isConfigure();
    const int iVst2Version
            = m_pVst2Plugin->vst2_dispatch(effGetVstVersion, 0, 0, nullptr, 0.0f);

    BaseHeader base_header;
    ::memset(&base_header, 0, sizeof(base_header));
    base_header.chunkMagic = *(VstInt32 *) cMagic;
    base_header.byteSize = 0;	// FIXME: see below...
    base_header.fxMagic = 0;	//
    base_header.version = 1;
    base_header.fxID = m_pVst2Plugin->get_unique_id();
    base_header.fxVersion = iVst2Version;

    // Estimate size of this section...
    base_header.byteSize = sizeof(base_header)
            - sizeof(base_header.chunkMagic)
            - sizeof(base_header.byteSize);

    Chunk chunk;
    if (bFxBank) {
            if (bChunked) {
                    get_chunk(chunk, 0);
                    base_header.byteSize += sizeof(chunk.size) + chunk.size;
                    base_header.fxMagic = *(VstInt32 *) chunkBankMagic;
            } else {
                    const int iNumParams  = pVst2Effect->numParams;
                    base_header.byteSize += pVst2Effect->numPrograms
                            * (sizeof(ProgHeader) + iNumParams * sizeof(float));
                    base_header.fxMagic = *(VstInt32 *) bankMagic;
            }
    } else {
            char szName[24]; ::memset(szName, 0, sizeof(szName));
            
            auto base = filePath.stem();
            ::strncpy(szName, base.c_str(), sizeof(szName) - 1);
            
            DMESSAGE("SZNAME = %s", szName);
            
            m_pVst2Plugin->vst2_dispatch(effSetProgramName, 0, 0, (void *) szName, 0.0f);

            if (bChunked) {
                    get_chunk(chunk, 1);
                    base_header.byteSize += sizeof(chunk.size) + chunk.size;
                    base_header.fxMagic = *(VstInt32 *) chunkPresetMagic;
            } else {
                    const int iNumParams  = pVst2Effect->numParams;
                    base_header.byteSize += sizeof(ProgHeader);
                    base_header.byteSize += iNumParams * sizeof(float);
                    base_header.fxMagic = *(VstInt32 *) fMagic;
            }
    }

//	fx_endian_swap(base_header.chunkMagic);
    fx_endian_swap(base_header.byteSize);
//	fx_endian_swap(base_header.fxMagic);
    fx_endian_swap(base_header.version);
    fx_endian_swap(base_header.fxID);
    fx_endian_swap(base_header.fxVersion);

    fwrite((char *) &base_header, sizeof(base_header), 1, fp);

    bool bResult = false;
    if (bFxBank) {
            if (bChunked)
                    bResult = save_bank_chunk(*fp, chunk);
            else
                    bResult = save_bank_progs(*fp);
    } else {
            if (bChunked)
                    bResult = save_prog_chunk(*fp, chunk);
            else
                    bResult = save_prog_params(*fp);
    }

    fclose(fp);
    return bResult;
}

#endif //  VST2_SUPPORT