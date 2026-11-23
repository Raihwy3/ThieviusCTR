#include "bis.h"
#include <algorithm>
#if defined(__3DS__)
#include <cstdio>
#include <vector>
#include <sys/stat.h>
namespace {
struct WorldReadAhead {
    std::string path;
    std::vector<byte> bytes;
    size_t ready=0;
    struct stat identity{};
    FILE* input=nullptr;
    CBinaryInputStream* consumer=nullptr;
    ~WorldReadAhead() {if(input)std::fclose(input);}
    void clear() {
        if(input)std::fclose(input);
        input=nullptr;consumer=nullptr;ready=0;path.clear();std::vector<byte>().swap(bytes);
    }
} worldReadAhead;
}
void CanePrefetchWorld3ds(const std::string& path) {
    auto& ahead=worldReadAhead;
    if(ahead.path!=path) {
        ahead.clear();ahead.path=path;
        if(stat(path.c_str(),&ahead.identity)!=0 || ahead.identity.st_size<=0)return;
        try {ahead.bytes.resize(std::min<size_t>(512*1024,size_t(ahead.identity.st_size)));}
        catch(const std::bad_alloc&) {return;} // optional under memory pressure
        ahead.input=std::fopen(path.c_str(),"rb");
        if(ahead.input)std::setvbuf(ahead.input,nullptr,_IONBF,0);
    }
    if(!ahead.input || ahead.ready==ahead.bytes.size())return;
    // One bounded read per rendered exit-animation frame, never a second world.
    const auto n=std::min<size_t>(64*1024,ahead.bytes.size()-ahead.ready);
    const auto got=std::fread(ahead.bytes.data()+ahead.ready,1,n,ahead.input);
    ahead.ready+=got;
    if(got!=n || ahead.ready==ahead.bytes.size()) {std::fclose(ahead.input);ahead.input=nullptr;}
}
#endif

CBinaryInputStream::CBinaryInputStream()
{
}

CBinaryInputStream::CBinaryInputStream(const std::string& filePath)
{
    OpenFile(filePath);
}

void CBinaryInputStream::OpenFile(const std::string& filePath)
{
    Close();

#if defined(__3DS__)
    // Large sequential reads amortize SD service calls across scalar BRX fields.
    if (!m_fileBuffer) m_fileBuffer = std::make_unique<char[]>(128 * 1024);
    // ReadBytes owns the read-ahead window. Avoid a second iostream buffer.
    file.rdbuf()->pubsetbuf(nullptr, 0);
#endif
    file.open(filePath, std::ios::in | std::ios::binary);

    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open binary input file: " + filePath);
    }

    m_bisk = BISK_File;
    m_pb = nullptr;
    m_cb = 0;
    m_ib = 0;
#if defined(__3DS__)
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 0) throw std::runtime_error("Failed to query binary input file size");
    m_fileSize = static_cast<std::size_t>(end);
    file.seekg(0, std::ios::beg);
    m_cb = m_fileSize; // empty read-ahead window; m_cb is its file offset
    auto& ahead=worldReadAhead;
    if(ahead.path==filePath) {
        if(ahead.input) {std::fclose(ahead.input);ahead.input=nullptr;}
        struct stat current{};
        if(stat(filePath.c_str(),&current)==0 && current.st_size==ahead.identity.st_size &&
           current.st_mtime==ahead.identity.st_mtime && current.st_ctime==ahead.identity.st_ctime && ahead.ready) {
            ahead.consumer=this;
            std::fprintf(stderr,"World prefetch: %lu KiB ready\n",static_cast<unsigned long>(ahead.ready/1024));
        } else ahead.clear();
    }
#endif
}

void CBinaryInputStream::OpenMemory(int cb, const void* pv)
{
    Close();

    if (cb < 0)
    {
        throw std::invalid_argument("CBinaryInputStream::OpenMemory received a negative size");
    }

    if (cb > 0 && !pv)
    {
        throw std::invalid_argument("CBinaryInputStream::OpenMemory received a null buffer");
    }

    m_bisk = BISK_Mem;
    m_pb = static_cast<const std::uint8_t*>(pv);
    m_cb = static_cast<std::size_t>(cb);
    m_ib = 0;
}

void CBinaryInputStream::ReadBytes(
    void* destination,
    std::size_t cb)
{
    if (cb == 0)
        return;

    if (!destination)
    {
        throw std::invalid_argument("CBinaryInputStream::ReadBytes received a null destination");
    }

    switch (m_bisk)
    {
        case BISK_File:
        {
            if (!file.is_open())
            {
                throw std::runtime_error("CBinaryInputStream file is not open");
            }

#if defined(__3DS__)
            if (m_ib > m_fileSize || cb > m_fileSize-m_ib)
                throw std::runtime_error("Unexpected end of binary input file");
            auto* output=static_cast<byte*>(destination);
            while(cb) {
                const auto windowSize=std::min<std::size_t>(128*1024,m_fileSize-m_cb);
                if(m_ib<m_cb || m_ib-m_cb>=windowSize) {
                    // One SD/iostream operation serves many scalar fields.
                    file.clear();file.seekg(static_cast<std::streamoff>(m_ib),std::ios::beg);
                    m_cb=m_ib;
                    const auto refill=std::min<std::size_t>(128*1024,m_fileSize-m_cb);
                    if(worldReadAhead.consumer==this && m_ib<=worldReadAhead.ready && refill<=worldReadAhead.ready-m_ib) {
                        std::memcpy(m_fileBuffer.get(),worldReadAhead.bytes.data()+m_ib,refill);
                    } else {
                        file.read(m_fileBuffer.get(),static_cast<std::streamsize>(refill));
                        if(file.gcount()!=static_cast<std::streamsize>(refill))
                            throw std::runtime_error("Unexpected end of binary input file");
                    }
                }
                const auto offset=m_ib-m_cb;
                const auto available=std::min<std::size_t>(128*1024,m_fileSize-m_cb)-offset;
                const auto count=std::min(cb,available);
                std::memcpy(output,m_fileBuffer.get()+offset,count);
                output+=count;m_ib+=count;cb-=count;
            }
            if(worldReadAhead.consumer==this && m_ib>=worldReadAhead.ready)worldReadAhead.clear();
#else
            if (cb > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
                throw std::length_error("CBinaryInputStream read is too large");

            const auto readSize = static_cast<std::streamsize>(cb);

            file.read(static_cast<char*>(destination), readSize);

            if (file.gcount() != readSize)
            {
                throw std::runtime_error("Unexpected end of binary input file");
            }
#endif

            break;
        }

        case BISK_Mem:
        {
            if (m_ib > m_cb || cb > m_cb - m_ib)
            {
                throw std::runtime_error(
                    "Unexpected end of binary memory stream");
            }

            std::memcpy(destination,m_pb + m_ib, cb);

            m_ib += cb;
            break;
        }

        default:
        {
            throw std::runtime_error( "CBinaryInputStream is not open");
        }
    }
}

std::size_t CBinaryInputStream::GetOffset()
{
    switch (m_bisk)
    {
    case BISK_File:
    {
        if (!file.is_open())
        {
            throw std::runtime_error(
                "CBinaryInputStream file is not open");
        }

#if defined(__3DS__)
        // All stream access goes through this class. Avoid tellg/seekoff for
        // every object alignment and descriptor while parsing the world.
        return m_ib;
#else
        const std::streampos position = file.tellg();

        if (position == std::streampos(-1))
        {
            throw std::runtime_error(
                "Failed to query binary input file position");
        }

        const std::streamoff offset =
            position - std::streampos(0);

        if (offset < 0)
        {
            throw std::runtime_error(
                "Binary input file returned a negative position");
        }

        return static_cast<std::size_t>(offset);
#endif
    }

    case BISK_Mem:
        return m_ib;

    default:
    {
        throw std::runtime_error(
            "CBinaryInputStream is not open");
    }
    }
}

void CBinaryInputStream::Align(int n)
{
    if (n <= 1)
        return;

    const std::size_t alignment =
        static_cast<std::size_t>(n);

    switch (m_bisk)
    {
    case BISK_File:
    {
        const std::size_t position = GetOffset();
        const std::size_t remainder =
            position % alignment;

        if (remainder != 0)
        {
            const std::size_t padding =
                alignment - remainder;

            if (padding > static_cast<std::size_t>(
                std::numeric_limits<std::streamoff>::max()))
            {
                throw std::length_error(
                    "Binary input alignment is too large");
            }

            Seek(position + padding);

            if (!file)
            {
                throw std::runtime_error(
                    "Failed to align binary input file");
            }
        }

        break;
    }

    case BISK_Mem:
    {
        if (m_ib > m_cb)
        {
            throw std::runtime_error(
                "Binary memory stream position exceeds its size");
        }

        const std::size_t remainder =
            m_ib % alignment;

        const std::size_t padding =
            remainder == 0
            ? 0
            : alignment - remainder;

        if (padding > m_cb - m_ib)
        {
            throw std::runtime_error(
                "Binary memory stream alignment exceeded buffer size");
        }

        m_ib += padding;
        break;
    }

    default:
    {
        throw std::runtime_error(
            "CBinaryInputStream is not open");
    }
    }
}

byte CBinaryInputStream::U8Read()
{
    byte value{};
    ReadBytes(&value, sizeof(value));
    return value;
}

uint16_t CBinaryInputStream::U16Read()
{
    uint16_t value{};
    ReadBytes(&value, sizeof(value));
    return value;
}

uint32_t CBinaryInputStream::U32Read()
{
    uint32_t value{};
    ReadBytes(&value, sizeof(value));
    return value;
}

int8_t CBinaryInputStream::S8Read()
{
    int8_t value{};
    ReadBytes(&value, sizeof(value));
    return value;
}

int16_t CBinaryInputStream::S16Read()
{
    int16_t value{};
    ReadBytes(&value, sizeof(value));
    return value;
}

int32_t CBinaryInputStream::S32Read()
{
    int32_t value{};
    ReadBytes(&value, sizeof(value));
    return value;
}

float CBinaryInputStream::F32Read()
{
    float value{};
    ReadBytes(&value, sizeof(value));
    return value;
}

glm::vec2 CBinaryInputStream::ReadVector2()
{
    // Separate statements guarantee stream read order.
    const float x = F32Read();
    const float y = F32Read();

    return glm::vec2(x, y);
}

glm::vec3 CBinaryInputStream::ReadVector()
{
    // Separate statements guarantee stream read order.
    const float x = F32Read();
    const float y = F32Read();
    const float z = F32Read();

    return glm::vec3(x, y, z);
}

glm::vec4 CBinaryInputStream::ReadVector4()
{
    // Separate statements guarantee stream read order.
    const float x = F32Read();
    const float y = F32Read();
    const float z = F32Read();
    const float w = F32Read();

    return glm::vec4(x, y, z, w);
}

glm::mat3 CBinaryInputStream::ReadMatrix()
{
    // The BRX MATRIX3 contains three consecutive VECTOR values:
    // 3 columns * 3 floats = 36 bytes.
    const glm::vec3 column0 = ReadVector();
    const glm::vec3 column1 = ReadVector();
    const glm::vec3 column2 = ReadVector();

    glm::mat3 matrix(1.0f);

    matrix[0] = column0;
    matrix[1] = column1;
    matrix[2] = column2;

    return matrix;
}

glm::mat4 CBinaryInputStream::ReadMatrix4()
{
    // The BRX MATRIX4 stores four consecutive three-float vectors:
    // 4 columns * 3 floats = 48 bytes.
    const glm::vec3 column0 = ReadVector();
    const glm::vec3 column1 = ReadVector();
    const glm::vec3 column2 = ReadVector();
    const glm::vec3 column3 = ReadVector();

    glm::mat4 matrix(1.0f);

    matrix[0] = glm::vec4(column0, 0.0f);
    matrix[1] = glm::vec4(column1, 0.0f);
    matrix[2] = glm::vec4(column2, 0.0f);
    matrix[3] = glm::vec4(column3, 1.0f);

    return matrix;
}

std::shared_ptr<char[]>
CBinaryInputStream::ReadStringAlloc()
{
    const uint16_t cch = U16Read();

    std::shared_ptr<char[]> text(
        new char[static_cast<std::size_t>(cch) + 1]());

    if (cch > 0)
    {
        ReadBytes(
            text.get(),
            static_cast<std::size_t>(cch));
    }

    text[cch] = '\0';

    return text;
}

std::string CBinaryInputStream::ReadStringSw()
{
    const uint16_t cch = U16Read();

    std::string text(static_cast<std::size_t>(cch), '\0');

    if (cch > 0)
        ReadBytes(text.data(), static_cast<std::size_t>(cch));

    return text;
}

std::string CBinaryInputStream::ReadFixedString32NoAlloc()
{
    char buffer[32]{};

    ReadBytes(
        buffer,
        sizeof(buffer));

    std::size_t length = 0;

    while (length < sizeof(buffer) &&
        buffer[length] != '\0')
    {
        ++length;
    }

    return std::string(buffer, length);
}

std::shared_ptr<char[]> CBinaryInputStream::ReadFixedString32()
{
    char buffer[32]{};

    ReadBytes(
        buffer,
        sizeof(buffer));

    if (buffer[0] == '\0')
        return nullptr;

    std::shared_ptr<char[]> text(new char[33]());

    std::memcpy(
        text.get(),
        buffer,
        sizeof(buffer));

    text[32] = '\0';

    return text;
}

void CBinaryInputStream::Close()
{
#if defined(__3DS__)
    if(worldReadAhead.consumer==this)worldReadAhead.clear();
#endif
    if (file.is_open())
        file.close();

    file.clear();

    m_bisk = BISK_Nil;
    m_pb = nullptr;
    m_cb = 0;
    m_ib = 0;
}

CBinaryInputStream::~CBinaryInputStream()
{
    Close();
}

std::size_t CBinaryInputStream::GetSize()
{
    if (m_bisk == BISK_Mem)
        return m_cb;

    if (m_bisk != BISK_File || !file.is_open())
        return 0;

#if defined(__3DS__)
    return m_fileSize;
#else
    const std::streampos position = file.tellg();
    file.clear();
    file.seekg(0, std::ios::end);
    const std::streampos end = file.tellg();
    if (position >= 0)
        file.seekg(position);
    return end >= 0 ? static_cast<std::size_t>(end) : 0;
#endif
}

std::size_t CBinaryInputStream::Remaining()
{
    const std::size_t size = GetSize();
    const std::size_t offset = GetOffset();
    return offset <= size ? size - offset : 0;
}

void CBinaryInputStream::Seek(std::size_t offset)
{
    switch (m_bisk)
    {
    case BISK_File:
        if (!file.is_open())
            throw std::runtime_error("CBinaryInputStream file is not open");
        if (offset > static_cast<std::size_t>((std::numeric_limits<std::streamoff>::max)()))
            throw std::length_error("Binary input seek is too large");
#if defined(__3DS__)
        // Keep the read-ahead window even across backward seeks within it.
        // The next read refills only if the requested bytes are outside it.
        m_ib=offset;
#else
        file.clear();
        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!file)
            throw std::runtime_error("Failed to seek binary input file");
#endif
        break;

    case BISK_Mem:
        if (offset > m_cb)
            throw std::runtime_error("Binary memory stream seek exceeded buffer size");
        m_ib = offset;
        break;

    default:
        throw std::runtime_error("CBinaryInputStream is not open");
    }
}
