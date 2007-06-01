#ifndef WORLD_ARRAY
#define WORLD_ARRAY

#include <vector>
#include <world/worldexc.h>

namespace madness {

    /// A simple, fixed dimension Vector 

    /// Eliminates memory allocation cost, is just POD so can be
    /// copied easily and allocated on the stack, and the known
    /// dimension permits agressive compiler optimizations.
    ///
    /// Actually, this behaves more like a vector.
    /// Why not call it that????????
    template <typename T, std::size_t N>
    class Vector {
    private:
        T v[N];
    public:
        typedef T* iterator;
        typedef const T* const_iterator;

        /// Default constructor does not initialize vector contents
        Vector() {};

        /// Initialize all elements to value t
        Vector(T t) {
            for (std::size_t i=0; i<N; i++) v[i] = t;
        };
        
        /// Construct from a C++ array of the same dimension
        Vector(const T (&t)[N])  {
            for (std::size_t i=0; i<N; i++) v[i] = t[i];
        };

        /// Construct from an STL vector of equal or greater length
        Vector(const std::vector<T> t) {
            MADNESS_ASSERT(t.size() >= N);
            for (std::size_t i=0; i<N; i++) v[i] = t[i];
        };
        
        /// Copy constructor is deep (since a vector is POD)
        Vector(const Vector<T,N>& other) {
            for (std::size_t i=0; i<N; i++) v[i] = other.v[i];
        };

        /// Assignment is deep (since a vector is POD)
        Vector& operator=(const Vector<T,N>& other) {
            for (std::size_t i=0; i<N; i++) v[i] = other.v[i];
            return *this;
        };

        /// Assignment is deep (since a vector is POD)
        Vector& operator=(const std::vector<T>& other) {
            for (std::size_t i=0; i<N; i++) v[i] = other[i];
            return *this;
        };

        /// Fill from scalar value
        Vector& operator=(T t) {
            for (std::size_t i=0; i<N; i++) v[i] = t;
            return *this;
        };

        /// Test for element-wise equality
        bool operator==(const Vector<T,N>& other) const {
            for (std::size_t i=0; i<N; i++) 
                if (v[i] != other.v[i]) return false;
            return true;
        };

        /// Same as !(*this==other)
        bool operator!=(const Vector<T,N>& other) const {
            return !(*this==other);
        };

        /// Tests a<b in sense of lexically ordered index

        /// Can be used to impose a standard ordering on vectors.
        bool operator<(const Vector<T,N>& other) const {
            for (std::size_t i=0; i<N; i++) {
                if (v[i] < other.v[i]) return true;
                else if (v[i] > other.v[i]) return false;
            }
            if (v[N-1] == other.v[N-1]) return false; // equality
            return true;
        };

        /// Indexing
        T& operator[](std::size_t i) {
            return v[i];
        };

        /// Indexing
        const T& operator[](std::size_t i) const {
            return v[i];
        };

        /// Element-wise multiplcation by a scalar

        /// Returns a new vector
        template <typename Q>
        Vector<T,N> operator*(Q q) const {
            Vector<T,N> r;
            for (std::size_t i=0; i<N; i++) r[i] = v[i] * q;
            return r;
        }

        /// In-place element-wise multiplcation by a scalar

        /// Returns a reference to this for chaining operations
        template <typename Q>
        Vector<T,N>& operator*=(Q q) {
            for (std::size_t i=0; i<N; i++) v[i] *= q;
            return *this;
        }

        /// Element-wise multiplcation by another vector
        
        /// Returns a new vector
        template <typename Q>
        Vector<T,N> operator*(const Vector<Q,N>& q) const {
            Vector<T,N> r;
            for (std::size_t i=0; i<N; i++) r[i] = v[i]*q[i];
            return r;
        }

        /// Element-wise addition of a scalar

        /// Returns a new vector
        template <typename Q>
        Vector<T,N> operator+(Q q) const {
            Vector<T,N> r;
            for (std::size_t i=0; i<N; i++) r[i] = v[i] + q;
            return r;
        }

        /// In-place element-wise addition of a scalar

        /// Returns a reference to this for chaining operations
        template <typename Q>
        Vector<T,N>& operator+=(Q q) {
            for (std::size_t i=0; i<N; i++) v[i] += q;
            return *this;
        }

        /// Element-wise addition of another vector

        /// Returns a new vector
        template <typename Q>
        Vector<T,N> operator+(const Vector<Q,N>& q) const {
            Vector<T,N> r;
            for (std::size_t i=0; i<N; i++) r[i] = v[i] + q[i];
            return r;
        }

        /// Element-wise subtraction of a scalar

        /// Returns a new vector
        template <typename Q>
        Vector<T,N> operator-(Q q) const {
            Vector<T,N> r;
            for (std::size_t i=0; i<N; i++) r[i] = v[i] - q;
            return r;
        }

        /// Element-wise subtraction of another vector

        /// Returns a new vector
        template <typename Q>
        Vector<T,N> operator-(const Vector<Q,N>& q) const {
            Vector<T,N> r;
            for (std::size_t i=0; i<N; i++) r[i] = v[i] - q[i];
            return r;
        }

        /// STL iterator support
        iterator begin() {
            return v;
        };

        /// STL iterator support
        const_iterator begin() const {
            return v;
        };

        /// STL iterator support
        iterator end() {
            return v+N;
        };

        /// STL iterator support
        const_iterator end() const {
            return v+N;
        };

        /// Length of the vector
        std::size_t size() const {
            return N;
        };
        
        /// Support for MADNESS serialization
        template <typename Archive>
        void serialize(Archive& ar) {
            ar & v;
        }

        /// Support for MADNESS hashing
        hashT hash() const {
            return madness::hash(v);
        };
    };

    /// Output vector to stream for human consumtion
    template <typename T,std::size_t N>
    std::ostream& operator<<(std::ostream& s, const Vector<T,N>& a) {
        s << "[";
        for (std::size_t i=0; i<N; i++) {
            s << a[i];
            if (i != (N-1)) s << ",";
        }
        s << "]";
        return s;
    }

    
    /// A simple, fixed-size, stack
    template <typename T,std::size_t N>
    class Stack {
    private:
        Vector<T,N> t;
        std::size_t n;
        
    public:
        Stack() : t(), n(0) {};
        
        void push(const T& value) {
            MADNESS_ASSERT(n < N);
            t[n++] = value;
        };
        
        T& pop() {
            MADNESS_ASSERT(n > 0);
            return t[--n];
        };

        std::size_t size() const {
            return n;
        };
    };


    // Sigh ...vector_factory already in use by tensor

    /// Returns a Vector<T,1> initialized from the arguments
    template <typename T>
    inline Vector<T,1> VectorFactory(const T& v0) {
        Vector<T,1> v;
        v[0] = v0;
        return v;
    }
    
    /// Returns a Vector<T,2> initialized from the arguments
    template <typename T>
    inline Vector<T,2> VectorFactory(const T& v0, const T& v1) {
        Vector<T,2> v;
        v[0] = v0;
        v[1] = v1;
        return v;
    }
    
    /// Returns a Vector<T,3> initialized from the arguments
    template <typename T>
    inline Vector<T,3> VectorFactory(const T& v0, const T& v1,
                                      const T& v2) {
        Vector<T,3> v;
        v[0] = v0;
        v[1] = v1;
        v[2] = v2;
        return v;
    }
    
    /// Returns a Vector<T,4> initialized from the arguments
    template <typename T>
    inline Vector<T,4> VectorFactory(const T& v0, const T& v1,
                                      const T& v2, const T& v3) {
        Vector<T,4> v;
        v[0] = v0;
        v[1] = v1;
        v[2] = v2;
        v[3] = v3;
        return v;
    }

    /// Returns a Vector<T,5> initialized from the arguments
    template <typename T>
    inline Vector<T,5> VectorFactory(const T& v0, const T& v1,
                                         const T& v2, const T& v3,
                                         const T& v4) {
        Vector<T,5> v;
        v[0] = v0;
        v[1] = v1;
        v[2] = v2;
        v[3] = v3;
        v[4] = v4;
        return v;
    }

    /// Returns a Vector<T,6> initialized from the arguments
    template <typename T>
    inline Vector<T,6> VectorFactory(const T& v0, const T& v1,
                                      const T& v2, const T& v3,
                                      const T& v4, const T& v5) {
        Vector<T,6> v;
        v[0] = v0;
        v[1] = v1;
        v[2] = v2;
        v[3] = v3;
        v[4] = v4;
        v[5] = v5;
        return v;
    }
}
#endif
