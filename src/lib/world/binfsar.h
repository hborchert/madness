#ifndef MAD_BINFSAR_H
#define MAD_BINFSAR_H

/// \file binfsar.h
/// \brief Implements archive wrapping a binary filestream

#include <fstream>
#include <world/archive.h>


namespace madness {
namespace archive {

    /// Wraps an archive around a binary file stream for output
class BinaryFstreamOutputArchive : public BaseOutputArchive {
    mutable std::ofstream os;
public:
    BinaryFstreamOutputArchive(const char* filename = 0,
                               std::ios_base::openmode mode = std::ios_base::binary | \
                                                              std::ios_base::out | std::ios_base::trunc) {
        if (filename) open(filename, mode);
    }

    template <class T>
    inline
    typename madness::enable_if< madness::is_serializable<T>, void >::type
    store(const T* t, long n) const {
        os.write((const char *) t, n*sizeof(T));
    }

    void open(const char* filename,
              std::ios_base::openmode mode = std::ios_base::binary | \
                                             std::ios_base::out |  std::ios_base::trunc) {
        os.open(filename, mode);
        store(ARCHIVE_COOKIE, strlen(ARCHIVE_COOKIE)+1);
    };

    void close() {
        os.close();
    };

    void flush() {
        os.flush();
    };
};


    /// Wraps an archive around a binary file stream for input
class BinaryFstreamInputArchive : public BaseInputArchive {
    mutable std::ifstream is;
public:
    BinaryFstreamInputArchive(const char* filename = 0, std::ios_base::openmode mode = std::ios_base::binary | std::ios_base::in)  {
        if (filename) open(filename, mode);
    }

    template <class T>
    inline
    typename madness::enable_if< madness::is_serializable<T>, void >::type
    load(T* t, long n) const {
        is.read((char *) t, n*sizeof(T));
    }

    void open(const char* filename,  std::ios_base::openmode mode = std::ios_base::binary | std::ios_base::in) {
        is.open(filename, mode);
        char cookie[255];
        int n = strlen(ARCHIVE_COOKIE)+1;
        load(cookie, n);
        if (strncmp(cookie,ARCHIVE_COOKIE,n) != 0)
            throw("BinaryFstreamInputArchive: open: not an archive?");
    };

    void close() {
        is.close();
    };
};
}
}
#endif
