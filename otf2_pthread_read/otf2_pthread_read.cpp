/*
 * This file is part of the Score-P software (http://www.score-p.org)
 *
 * Copyright (c) 2009-2013,
 * RWTH Aachen University, Germany
 *
 * Copyright (c) 2009-2013,
 * Gesellschaft fuer numerische Simulation mbH Braunschweig, Germany
 *
 * Copyright (c) 2009-2014,
 * Technische Universitaet Dresden, Germany
 *
 * Copyright (c) 2009-2013,
 * University of Oregon, Eugene, USA
 *
 * Copyright (c) 2009-2014,
 * Forschungszentrum Juelich GmbH, Germany
 *
 * Copyright (c) 2009-2013,
 * German Research School for Simulation Sciences GmbH, Juelich/Aachen, Germany
 *
 * Copyright (c) 2009-2013,
 * Technische Universitaet Muenchen, Germany
 *
 * This software may be modified and distributed under the terms of
 * a BSD-style license.  See the COPYING file in the package base
 * directory for details.
 *
 */
#include <cstddef>
extern "C"
{
    #include <otf2/otf2.h>
    #include <pthread.h>
}

#include <vector>
#include <set>
#include <algorithm>
#include <thread>
#include <iterator>
#include <cstdio>
#include <cinttypes>
#include <cstdlib>
#include <iostream>

OTF2_CallbackCode
local_enter_cb( OTF2_LocationRef    location,
                OTF2_TimeStamp      time,
                uint64_t            eventPosition,
                void*               userData,
                OTF2_AttributeList* attributeList,
                OTF2_RegionRef      region );

OTF2_CallbackCode
local_leave_cb( OTF2_LocationRef    location,
                OTF2_TimeStamp      time,
                uint64_t            eventPosition,
                void*               userData,
                OTF2_AttributeList* attributeList,
                OTF2_RegionRef      region );

OTF2_CallbackCode
GlobDefLocation_Register( void*                 userData,
                          OTF2_LocationRef      location,
                          OTF2_StringRef        name,
                          OTF2_LocationType     locationType,
                          uint64_t              numberOfEvents,
                          OTF2_LocationGroupRef locationGroup );

 OTF2_CallbackCode
 def_local_string (void *userData, OTF2_StringRef self, const char *string);

class LocalReader
{
public:
    ~LocalReader()
    {
        if(nevents > 0)
        {
            std::cout << "Read " << nevents << " events.\n";
            std::cout << "Read " << nstring_defs << " string defs.\n";
        }
    }

    inline void
    read_definitions(OTF2_Reader* reader, const std::vector<size_t> & locations)
    {
        bool successful_open_def_files = OTF2_Reader_OpenDefFiles( reader ) == OTF2_SUCCESS;

        OTF2_DefReaderCallbacks* def_callbacks = OTF2_DefReaderCallbacks_New();

        OTF2_DefReaderCallbacks_SetStringCallback(def_callbacks, def_local_string);

        for (auto location: locations)
        {
            if ( successful_open_def_files )
            {
                OTF2_DefReader* def_reader = OTF2_Reader_GetDefReader( reader, location );
                OTF2_Reader_RegisterDefCallbacks(reader, def_reader, def_callbacks, this);
                if ( def_reader )
                {
                    uint64_t def_reads = 0;
                    OTF2_Reader_ReadAllLocalDefinitions( reader,
                                                        def_reader,
                                                        &def_reads );
                    OTF2_Reader_CloseDefReader( reader, def_reader );
                }
            }
            OTF2_EvtReader* evt_reader = OTF2_Reader_GetEvtReader( reader, location );
        }
        if ( successful_open_def_files )
        {
            OTF2_Reader_CloseDefFiles( reader );
        }
    }

    inline void
    read_events(OTF2_Reader* reader, const std::vector<size_t> & locations)
    {
        OTF2_Reader_OpenEvtFiles( reader );

        OTF2_EvtReaderCallbacks* evt_callbacks = OTF2_EvtReaderCallbacks_New();

        OTF2_EvtReaderCallbacks_SetEnterCallback(evt_callbacks,
                                                    local_enter_cb);

        OTF2_EvtReaderCallbacks_SetLeaveCallback(evt_callbacks,
                                                    local_leave_cb);

        for (auto location: locations)
        {
            OTF2_EvtReader *  evt_reader = OTF2_Reader_GetEvtReader( reader, location);
            OTF2_Reader_RegisterEvtCallbacks(reader,
                                                evt_reader,
                                                evt_callbacks,
                                                this);
            if(evt_reader)
            {
                uint64_t events_read;
                OTF2_Reader_ReadAllLocalEvents(reader,
                                                evt_reader,
                                                &events_read);

                OTF2_Reader_CloseEvtReader(reader,
                                            evt_reader);
            }
        }
        OTF2_EvtReaderCallbacks_Delete( evt_callbacks );
        OTF2_Reader_CloseEvtFiles( reader );
    }

    void
    operator() (OTF2_Reader* reader, std::vector<size_t> locations)
    {
        uint64_t number_of_locations_to_read = 0;
        for (auto location: locations)
        {
            number_of_locations_to_read++;
            OTF2_Reader_SelectLocation( reader, location );
        }

        if ( number_of_locations_to_read > 0 )
        {
            read_definitions(reader, locations);
            read_events(reader, locations);
        }
    }

    unsigned long int nevents = 0;
    unsigned long int nstring_defs = 0;
};

class Reader
{
public:
    Reader(const char * path, size_t nthreads=std::thread::hardware_concurrency())
    :m_reader(OTF2_Reader_Open(path)),
     m_nthreads(nthreads)
    {
        OTF2_Reader_SetSerialCollectiveCallbacks( m_reader );

        uint64_t number_of_locations;
        OTF2_Reader_GetNumberOfLocations( m_reader,
                                        &number_of_locations );

        m_locations.reserve(number_of_locations);

        OTF2_GlobalDefReader* global_def_reader = OTF2_Reader_GetGlobalDefReader( m_reader );

        OTF2_GlobalDefReaderCallbacks* global_def_callbacks = OTF2_GlobalDefReaderCallbacks_New();

        OTF2_GlobalDefReaderCallbacks_SetLocationCallback( global_def_callbacks,
                                                        &GlobDefLocation_Register );

        OTF2_Reader_RegisterGlobalDefCallbacks( m_reader,
                                                global_def_reader,
                                                global_def_callbacks,
                                                this );

        OTF2_GlobalDefReaderCallbacks_Delete( global_def_callbacks );

        uint64_t definitions_read = 0;
        OTF2_Reader_ReadAllGlobalDefinitions( m_reader,
                                            global_def_reader,
                                            &definitions_read );

        OTF2_Reader_CloseGlobalDefReader( m_reader,
                                          global_def_reader );
    }
    ~Reader()
    {
        OTF2_Reader_Close( m_reader );
    }

    void read()
    {
        std::vector<std::thread> workers;

        size_t locations_per_thread = m_locations.size() / m_nthreads;
        size_t rest_locations = m_locations.size() - locations_per_thread * m_nthreads;

        std::vector<size_t> thread_location_count(m_nthreads, locations_per_thread);
        for(int i = 0; rest_locations > 0; rest_locations--, i++)
        {
            thread_location_count[i % m_nthreads]++;
        }

        auto src_begin = m_locations.begin();
        for(int i = 0; i < thread_location_count.size(); i++)
        {
            auto thread_locations = std::vector<size_t>(src_begin, src_begin + thread_location_count[i]);
            workers.emplace_back(LocalReader(), m_reader, thread_locations);
            src_begin += thread_location_count[i];
        }
        for(auto & w: workers)
        {
            w.join();
        }
    }

    std::vector<size_t> & locations() { return m_locations; }

private:
    std::vector<size_t> m_locations;
    OTF2_Reader* m_reader;
    size_t m_nthreads;
};

OTF2_CallbackCode
GlobDefLocation_Register( void*                 userData,
                          OTF2_LocationRef      location,
                          OTF2_StringRef        name,
                          OTF2_LocationType     locationType,
                          uint64_t              numberOfEvents,
                          OTF2_LocationGroupRef locationGroup )
{
    Reader * reader = static_cast<Reader *>(userData);
    auto & locations = reader->locations();
    if ( locations.size() == locations.capacity() )
    {
        return OTF2_CALLBACK_INTERRUPT;
    }
    locations.push_back(location);
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode
local_enter_cb( OTF2_LocationRef    location,
                OTF2_TimeStamp      time,
                uint64_t            eventPosition,
                void*               userData,
                OTF2_AttributeList* attributeList,
                OTF2_RegionRef      region )
{
    if(auto * worker = static_cast<LocalReader *>(userData))
    {
        worker->nevents++;
    }
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode
local_leave_cb( OTF2_LocationRef    location,
                OTF2_TimeStamp      time,
                uint64_t            eventPosition,
                void*               userData,
                OTF2_AttributeList* attributeList,
                OTF2_RegionRef      region )
{
    if(auto * worker = static_cast<LocalReader *>(userData))
    {
        worker->nevents++;
    }
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode
def_local_string (void *userData,
                  OTF2_StringRef self,
                  const char *string)
{
    if(auto * worker = static_cast<LocalReader *>(userData))
    {
        worker->nstring_defs++;
    }
    return OTF2_CALLBACK_SUCCESS;
}

int main(int argc, char * argv[])
{
    Reader r(argv[1], std::stoul(argv[2]));
    r.read();
    return 0;
}