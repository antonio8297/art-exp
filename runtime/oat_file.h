/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_OAT_FILE_H_
#define ART_RUNTIME_OAT_FILE_H_

#include <string>
#include <vector>

#include "base/stringpiece.h"
#include "dex_file.h"
#include "invoke_type.h"
#include "mem_map.h"
#include "mirror/class.h"
#include "oat.h"
#include "os.h"

namespace art {

class BitVector;
class ElfFile;
class MemMap;
class OatMethodOffsets;
class OatHeader;

class OatFile {
 public:
  // Open an oat file. Returns NULL on failure.  Requested base can
  // optionally be used to request where the file should be loaded.
  static OatFile* Open(const std::string& filename,
                       const std::string& location,
                       byte* requested_base,
                       bool executable,
                       std::string* error_msg);

  // Open an oat file from an already opened File.
  // Does not use dlopen underneath so cannot be used for runtime use
  // where relocations may be required. Currently used from
  // ImageWriter which wants to open a writable version from an existing
  // file descriptor for patching.
  static OatFile* OpenWritable(File* file, const std::string& location, std::string* error_msg);
  // Opens an oat file from an already opened File. Maps it PROT_READ, MAP_PRIVATE.
  static OatFile* OpenReadable(File* file, const std::string& location, std::string* error_msg);

  // Open an oat file backed by a std::vector with the given location.
  static OatFile* OpenMemory(std::vector<uint8_t>& oat_contents,
                             const std::string& location,
                             std::string* error_msg);

  ~OatFile();

  ElfFile* GetElfFile() const {
    CHECK_NE(reinterpret_cast<uintptr_t>(elf_file_.get()), reinterpret_cast<uintptr_t>(nullptr))
        << "Cannot get an elf file from " << GetLocation();
    return elf_file_.get();
  }

  const std::string& GetLocation() const {
    return location_;
  }

  const OatHeader& GetOatHeader() const;

  class OatDexFile;

  class OatMethod {
   public:
    void LinkMethod(mirror::ArtMethod* method) const SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

    uint32_t GetCodeOffset() const {
      return code_offset_;
    }
    uint32_t GetNativeGcMapOffset() const {
      return native_gc_map_offset_;
    }

    const void* GetPortableCode() const {
      // TODO: encode whether code is portable/quick in flags within OatMethod.
      if (kUsePortableCompiler) {
        return GetOatPointer<const void*>(code_offset_);
      } else {
        return nullptr;
      }
    }

    const void* GetQuickCode() const {
      if (kUsePortableCompiler) {
        return nullptr;
      } else {
        return GetOatPointer<const void*>(code_offset_);
      }
    }

    uint32_t GetPortableCodeSize() const {
      // TODO: With Quick, we store the size before the code. With Portable, the code is in a .o
      // file we don't manage ourselves. ELF symbols do have a concept of size, so we could capture
      // that and store it somewhere, such as the OatMethod.
      return 0;
    }
    uint32_t GetQuickCodeSize() const;

    const uint8_t* GetNativeGcMap() const {
      return GetOatPointer<const uint8_t*>(native_gc_map_offset_);
    }

    size_t GetFrameSizeInBytes() const;
    uint32_t GetCoreSpillMask() const;
    uint32_t GetFpSpillMask() const;
    uint32_t GetMappingTableOffset() const;
    uint32_t GetVmapTableOffset() const;
    const uint8_t* GetMappingTable() const;
    const uint8_t* GetVmapTable() const;

    ~OatMethod();

    // Create an OatMethod with offsets relative to the given base address
    OatMethod(const byte* base,
              const uint32_t code_offset,
              const uint32_t gc_map_offset);

   private:
    template<class T>
    T GetOatPointer(uint32_t offset) const {
      if (offset == 0) {
        return NULL;
      }
      return reinterpret_cast<T>(begin_ + offset);
    }

    const byte* begin_;

    uint32_t code_offset_;
    uint32_t native_gc_map_offset_;

    friend class OatClass;
  };

  class OatClass {
   public:
    mirror::Class::Status GetStatus() const {
      return status_;
    }

    OatClassType GetType() const {
      return type_;
    }

    // Get the OatMethod entry based on its index into the class
    // defintion. direct methods come first, followed by virtual
    // methods. note that runtime created methods such as miranda
    // methods are not included.
    const OatMethod GetOatMethod(uint32_t method_index) const;

   private:
    OatClass(const OatFile* oat_file,
             mirror::Class::Status status,
             OatClassType type,
             uint32_t bitmap_size,
             const uint32_t* bitmap_pointer,
             const OatMethodOffsets* methods_pointer);

    const OatFile* const oat_file_;

    const mirror::Class::Status status_;

    const OatClassType type_;

    const uint32_t* const bitmap_;

    const OatMethodOffsets* methods_pointer_;

    friend class OatDexFile;
  };

  class OatDexFile {
   public:
    // Opens the DexFile referred to by this OatDexFile from within the containing OatFile.
    const DexFile* OpenDexFile(std::string* error_msg) const;

    // Returns the size of the DexFile refered to by this OatDexFile.
    size_t FileSize() const;

    // Returns original path of DexFile that was the source of this OatDexFile.
    const std::string& GetDexFileLocation() const {
      return dex_file_location_;
    }

    // Returns checksum of original DexFile that was the source of this OatDexFile;
    uint32_t GetDexFileLocationChecksum() const {
      return dex_file_location_checksum_;
    }

    // Returns the OatClass for the class specified by the given DexFile class_def_index.
    OatClass GetOatClass(uint16_t class_def_index) const;

    ~OatDexFile();

   private:
    OatDexFile(const OatFile* oat_file,
               const std::string& dex_file_location,
               uint32_t dex_file_checksum,
               const byte* dex_file_pointer,
               const uint32_t* oat_class_offsets_pointer);

    const OatFile* const oat_file_;
    const std::string dex_file_location_;
    const uint32_t dex_file_location_checksum_;
    const byte* const dex_file_pointer_;
    const uint32_t* const oat_class_offsets_pointer_;

    friend class OatFile;
    DISALLOW_COPY_AND_ASSIGN(OatDexFile);
  };

  const OatDexFile* GetOatDexFile(const char* dex_location,
                                  const uint32_t* const dex_location_checksum,
                                  bool exception_if_not_found = true) const;

  std::vector<const OatDexFile*> GetOatDexFiles() const;

  size_t Size() const {
    return End() - Begin();
  }

  const byte* Begin() const;
  const byte* End() const;

 private:
  static void CheckLocation(const std::string& location);

  static OatFile* OpenDlopen(const std::string& elf_filename,
                             const std::string& location,
                             byte* requested_base,
                             std::string* error_msg);

  static OatFile* OpenElfFile(File* file,
                              const std::string& location,
                              byte* requested_base,
                              bool writable,
                              bool executable,
                              std::string* error_msg);

  explicit OatFile(const std::string& filename);
  bool Dlopen(const std::string& elf_filename, byte* requested_base, std::string* error_msg);
  bool ElfFileOpen(File* file, byte* requested_base, bool writable, bool executable,
                   std::string* error_msg);
  bool Setup(std::string* error_msg);

  // The oat file name.
  //
  // The image will embed this to link its associated oat file.
  const std::string location_;

  // Pointer to OatHeader.
  const byte* begin_;

  // Pointer to end of oat region for bounds checking.
  const byte* end_;

  // Backing memory map for oat file during when opened by ElfWriter during initial compilation.
  std::unique_ptr<MemMap> mem_map_;

  // Backing memory map for oat file during cross compilation.
  std::unique_ptr<ElfFile> elf_file_;

  // dlopen handle during runtime.
  void* dlopen_handle_;

  // NOTE: We use a StringPiece as the key type to avoid a memory allocation on every lookup
  // with a const char* key.
  typedef SafeMap<StringPiece, const OatDexFile*> Table;
  Table oat_dex_files_;

  friend class OatClass;
  friend class OatDexFile;
  friend class OatDumper;  // For GetBase and GetLimit
  DISALLOW_COPY_AND_ASSIGN(OatFile);
};

}  // namespace art

#endif  // ART_RUNTIME_OAT_FILE_H_
