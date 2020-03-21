#include "otf2/OTF2_Reader.h"
#include <cstddef>
extern "C"
{
    #include <otf2/otf2.h>
    #include <omp.h>
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
Enter_print( OTF2_LocationRef    location,
             OTF2_TimeStamp      time,
             void*               userData,
             OTF2_AttributeList* attributes,
             OTF2_RegionRef      region );

OTF2_CallbackCode
Leave_print( OTF2_LocationRef    location,
             OTF2_TimeStamp      time,
             void*               userData,
             OTF2_AttributeList* attributes,
             OTF2_RegionRef      region );

OTF2_CallbackCode
GlobDefLocation_Register( void*                 userData,
                          OTF2_LocationRef      location,
                          OTF2_StringRef        name,
                          OTF2_LocationType     locationType,
                          uint64_t              numberOfEvents,
                          OTF2_LocationGroupRef locationGroup );

class Worker
{
public:
    void
    operator() (OTF2_Reader* reader, std::vector<size_t> locations)
    {
        uint64_t number_of_locations_to_read = 0;
        for (auto location: locations)
        {
            number_of_locations_to_read++;
            OTF2_Reader_SelectLocation( reader, location );
        }

        bool successful_open_def_files = OTF2_Reader_OpenDefFiles( reader ) == OTF2_SUCCESS;
        OTF2_Reader_OpenEvtFiles( reader );

        for (auto location: locations)
        {
            if ( successful_open_def_files )
            {
                OTF2_DefReader* def_reader =
                    OTF2_Reader_GetDefReader( reader, location );
                if ( def_reader )
                {
                    uint64_t def_reads = 0;
                    OTF2_Reader_ReadAllLocalDefinitions( reader,
                                                        def_reader,
                                                        &def_reads );
                    OTF2_Reader_CloseDefReader( reader, def_reader );
                }
            }
            // std::cout << "Thread " << std::this_thread::get_id() << " " << location << '\n';
            OTF2_EvtReader* evt_reader =
                OTF2_Reader_GetEvtReader( reader, location );
        }
        // return;
        if ( successful_open_def_files )
        {
            OTF2_Reader_CloseDefFiles( reader );
        }

        if ( number_of_locations_to_read > 0 )
        {
            OTF2_GlobalEvtReader* global_evt_reader = OTF2_Reader_GetGlobalEvtReader( reader );

            OTF2_GlobalEvtReaderCallbacks* event_callbacks = OTF2_GlobalEvtReaderCallbacks_New();

            OTF2_GlobalEvtReaderCallbacks_SetEnterCallback( event_callbacks,
                                                            &Enter_print );
            OTF2_GlobalEvtReaderCallbacks_SetLeaveCallback( event_callbacks,
                                                            &Leave_print );
            OTF2_Reader_RegisterGlobalEvtCallbacks( reader,
                                                    global_evt_reader,
                                                    event_callbacks,
                                                    NULL );

            OTF2_GlobalEvtReaderCallbacks_Delete( event_callbacks );
            uint64_t events_read = 0;
            OTF2_Reader_ReadAllGlobalEvents( reader,
                                             global_evt_reader,
                                             &events_read );

            OTF2_Reader_CloseGlobalEvtReader( reader, global_evt_reader );
            std::cout << "Thread " << std::this_thread::get_id() << " " << events_read << '\n';
        }
        OTF2_Reader_CloseEvtFiles( reader );
    }
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
            workers.emplace_back(Worker(), m_reader, thread_locations);
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
    std::cout << "loc " << location << '\n';
    return OTF2_CALLBACK_SUCCESS;
}

OTF2_CallbackCode
Enter_print( OTF2_LocationRef    location,
             OTF2_TimeStamp      time,
             void*               userData,
             OTF2_AttributeList* attributes,
             OTF2_RegionRef      region )
{
    // Worker * worker = static_cast<Worker *>(userData);
    // worker->nevents++;

    return OTF2_CALLBACK_SUCCESS;
}
OTF2_CallbackCode
Leave_print( OTF2_LocationRef    location,
             OTF2_TimeStamp      time,
             void*               userData,
             OTF2_AttributeList* attributes,
             OTF2_RegionRef      region )
{
    // Worker * worker = static_cast<Worker *>(userData);
    // worker->nevents++;

    return OTF2_CALLBACK_SUCCESS;
}

int main(int argc, char * argv[])
{
    Reader r(argv[1], 2);
    r.read();
    return 0;
}