/**
 * This file is part of the CernVM File System.
 */

#ifndef TEST_UNITTESTS_TESTUTIL_H_
#define TEST_UNITTESTS_TESTUTIL_H_

#include <gtest/gtest.h>

#include <sys/types.h>

#include <ctime>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "catalog_mgr.h"
#include "crypto/hash.h"
#include "directory_entry.h"
#include "ingestion/ingestion_source.h"
#include "history.h"
#include "object_fetcher.h"
#include "upload_facility.h"
#include "util/atomic.h"

pid_t GetParentPid(const pid_t pid);
std::string GetProcessname(const pid_t pid);
std::string GetExecutablePath(const std::string &exe_name);

unsigned GetNoUsedFds();
std::string ShowOpenFiles();

time_t t(const int day, const int month, const int year);
shash::Any h(const std::string &hash,
             const shash::Suffix suffix = shash::kSuffixNone);

namespace catalog {

class DirectoryEntryTestFactory {
 public:
  struct Metadata {
    std::string name;
    unsigned int mode;
    uid_t uid;
    gid_t gid;
    uint64_t size;
    time_t mtime;
    std::string symlink;
    uint32_t linkcount;
    bool has_xattrs;
    bool is_hidden;
    shash::Any checksum;
  };

 public:
  static catalog::DirectoryEntry RegularFile(
      const string &name = "",
      unsigned size = 4096,
      shash::Any content_hash = shash::Any());
  static catalog::DirectoryEntry Directory(
      const string &name = "", unsigned size = 4096,
      shash::Any content_hash = shash::Any(),
      bool is_nested_catalog_mountpint = false);
  static catalog::DirectoryEntry Symlink(const string &name = "",
                                         unsigned size = 4096,
                                         const string &symlink_path = "");
  static catalog::DirectoryEntry ChunkedFile(
      shash::Any content_hash = shash::Any());
  static catalog::DirectoryEntry ExternalFile();
  static catalog::DirectoryEntry SpecialFile();
  static catalog::DirectoryEntry Make(const Metadata &metadata);
};

}  // namespace catalog

class PolymorphicConstructionUnittestAdapter {
 public:
  template <class AbstractProductT, class ConcreteProductT>
  static void RegisterPlugin() {
    AbstractProductT::template RegisterPlugin<ConcreteProductT>();
  }

  template <class AbstractProductT>
  static void UnregisterAllPlugins() {
    AbstractProductT::UnregisterAllPlugins();
  }
};


//------------------------------------------------------------------------------


static const char* g_sandbox_path    = "/tmp/cvmfs_mockuploader";
static const char* g_sandbox_tmp_dir = "/tmp/cvmfs_mockuploader/tmp";
static inline upload::SpoolerDefinition MockSpoolerDefinition(
  bool generate_legacy_bulk_chunks = true)
{
  const size_t      min_chunk_size   = 512000;
  const size_t      avg_chunk_size   = 2 * min_chunk_size;
  const size_t      max_chunk_size   = 4 * min_chunk_size;

  return upload::SpoolerDefinition("mock," + std::string(g_sandbox_path) + "," +
                                             std::string(g_sandbox_tmp_dir),
                                   shash::kSha1,
                                   zlib::kZlibDefault,
                                   generate_legacy_bulk_chunks,
                                   true,
                                   min_chunk_size,
                                   avg_chunk_size,
                                   max_chunk_size);
}


/**
 * This is a simple base class for a mocked uploader. It implements only the
 * very common parts and takes care of the internal instrumentation of
 * PolymorphicConstruction.
 */
template <class DerivedT>  // curiously recurring template pattern
class AbstractMockUploader : public upload::AbstractUploader {
 protected:
  static const bool not_implemented = false;

 public:
  static const std::string sandbox_path;
  static const std::string sandbox_tmp_dir;

 public:
  explicit AbstractMockUploader(
    const upload::SpoolerDefinition &spooler_definition)
    : AbstractUploader(spooler_definition)
  { }

  static DerivedT* MockConstruct() {
    PolymorphicConstructionUnittestAdapter::RegisterPlugin<
                                                      upload::AbstractUploader,
                                                      DerivedT>();
    DerivedT* result = dynamic_cast<DerivedT*>(
      AbstractUploader::Construct(MockSpoolerDefinition()));
    PolymorphicConstructionUnittestAdapter::UnregisterAllPlugins<
                                                    upload::AbstractUploader>();
    return result;
  }

  static bool WillHandle(const upload::SpoolerDefinition &spooler_definition) {
    return spooler_definition.driver_type == upload::SpoolerDefinition::Mock;
  }

  virtual void DoUpload(const std::string  &remote_path,
                        IngestionSource  *source,
                        const CallbackTN   *callback = NULL) {
    assert(AbstractMockUploader::not_implemented);
  }

  virtual upload::UploadStreamHandle* InitStreamedUpload(
                                            const CallbackTN *callback = NULL) {
    assert(AbstractMockUploader::not_implemented);
    return NULL;
  }

  virtual void StreamedUpload(upload::UploadStreamHandle *handle,
                              upload::AbstractUploader::UploadBuffer buffer,
                              const CallbackTN *callback = NULL) {
    assert(AbstractMockUploader::not_implemented);
  }

  virtual void FinalizeStreamedUpload(upload::UploadStreamHandle *handle,
                                      const shash::Any           &content_hash)
  {
    assert(AbstractMockUploader::not_implemented);
  }

  virtual void DoRemoveAsync(const std::string &file_to_delete) {
    assert(AbstractMockUploader::not_implemented);
  }

  virtual bool Peek(const std::string &path) {
    assert(AbstractMockUploader::not_implemented);
  }

  virtual bool Mkdir(const std::string &path) {
    assert(AbstractMockUploader::not_implemented);
  }

  virtual bool PlaceBootstrappingShortcut(const shash::Any &object) {
    assert(AbstractMockUploader::not_implemented);
  }

  virtual unsigned int GetNumberOfErrors() const {
    assert(AbstractMockUploader::not_implemented);
  }
};

template <class DerivedT>
const std::string AbstractMockUploader<DerivedT>::sandbox_path =
  g_sandbox_path;
template <class DerivedT>
const std::string AbstractMockUploader<DerivedT>::sandbox_tmp_dir =
  g_sandbox_tmp_dir;


//------------------------------------------------------------------------------


template <class ObjectT>
class MockObjectStorage {
 private:
  typedef std::map<shash::Any, ObjectT*> AvailableObjects;
  static AvailableObjects available_objects;

  typedef std::map<std::string, ObjectT*> AvailablePathObjects;
  static AvailablePathObjects available_paths;

 public:
  static std::set<shash::Any> *s_deleted_objects;

  friend class MockObjectFetcher;

 public:
  static void Reset() {
    MockObjectStorage::UnregisterObjects();
    s_deleted_objects = NULL;
    ObjectT::ResetGlobalState();
  }

  static void RegisterObject(const shash::Any &hash, ObjectT *object) {
    ASSERT_FALSE(Exists(hash)) << "exists already: " << hash.ToString();
    MockObjectStorage::available_objects[hash] = object;
  }

  static void RegisterPath(const std::string &path, ObjectT *object) {
    ASSERT_FALSE(Exists(path)) << "exists already: " << path;
    MockObjectStorage::available_paths[path] = object;
  }

  static void UnregisterObjects() {
    typename MockObjectStorage<ObjectT>::AvailableObjects::const_iterator
      i, iend;
    for (i    = MockObjectStorage<ObjectT>::available_objects.begin(),
         iend = MockObjectStorage<ObjectT>::available_objects.end();
         i != iend; ++i)
    {
      delete i->second;
    }
    MockObjectStorage<ObjectT>::available_objects.clear();

    typename MockObjectStorage<ObjectT>::AvailablePathObjects::const_iterator
      j, jend;
    for (j    = MockObjectStorage<ObjectT>::available_paths.begin(),
         jend = MockObjectStorage<ObjectT>::available_paths.end();
         j != jend; ++j)
    {
      delete j->second;
    }
    MockObjectStorage<ObjectT>::available_paths.clear();
  }

  static ObjectT* Get(const shash::Any &hash) {
    return (Exists(hash) && !IsDeleted(hash))
      ? available_objects[hash]
      : NULL;
  }

  static ObjectT* Get(const std::string &path) {
    return (Exists(path))
      ? available_paths[path]
      : NULL;
  }

 protected:
  static bool IsDeleted(const shash::Any &hash) {
    return s_deleted_objects != NULL &&
           s_deleted_objects->find(hash) != s_deleted_objects->end();
  }

  static bool Exists(const shash::Any &hash) {
    return available_objects.find(hash) != available_objects.end() &&
           !IsDeleted(hash);
  }

  static bool Exists(const std::string &path) {
    return available_paths.find(path) != available_paths.end();
  }
};

template <class ObjectT>
typename MockObjectStorage<ObjectT>::AvailableObjects
  MockObjectStorage<ObjectT>::available_objects;

template <class ObjectT>
typename MockObjectStorage<ObjectT>::AvailablePathObjects
  MockObjectStorage<ObjectT>::available_paths;

template <class ObjectT>
std::set<shash::Any>* MockObjectStorage<ObjectT>::s_deleted_objects;


//------------------------------------------------------------------------------


namespace manifest {
class Manifest;
}

namespace swissknife {
class CatalogTraversalParams;
}


/**
 * This is a minimal mock of a Catalog class.
 */
class MockCatalog : public MockObjectStorage<MockCatalog> {
 public:
  static const std::string rhs;
  static const shash::Any  root_hash;
  static atomic_int32      instances;

 public:
  struct NestedCatalog {
    PathString   mountpoint;
    shash::Any   hash;
    MockCatalog *child;
    uint64_t     size;
  };
  typedef std::vector<NestedCatalog> NestedCatalogList;

  struct File {
    shash::Any  hash;
    size_t      size;
    shash::Md5  path_hash;
    shash::Md5  parent_hash;
    string      name;
    bool        is_nested_catalog_mountpoint;

    File() : hash(shash::Any()), size(0), path_hash(shash::Md5()),
        parent_hash(shash::Md5()), name(""), is_nested_catalog_mountpoint(false)
    { }

    File(const shash::Any &hash, size_t size, const string &parent_path,
         const string &name) :
      hash(hash), size(size), name(name), is_nested_catalog_mountpoint(false)
    {
      if (parent_path == "" && name == "") {
        parent_hash = shash::Md5("", 0);
        path_hash = shash::Md5("", 0);
        is_nested_catalog_mountpoint = true;
      } else {
        string full_path = parent_path + "/" + name;
        parent_hash = shash::Md5(parent_path.c_str(), parent_path.length());
        path_hash = shash::Md5(full_path.c_str(), full_path.length());
      }
    }

    catalog::DirectoryEntry ToDirectoryEntry() const {
      bool is_directory = hash.IsNull();
      if (is_directory)
        return catalog::DirectoryEntryTestFactory::Directory(name, size, hash,
                                                  is_nested_catalog_mountpoint);
      return catalog::DirectoryEntryTestFactory::RegularFile(name, size, hash);
    }
  };
  typedef std::vector<File> FileList;

  struct Chunk {
    shash::Any  hash;
    size_t      size;
  };
  typedef std::vector<Chunk> ChunkList;

  typedef std::vector<shash::Any> HashVector;

 public:
  static void ResetGlobalState();

  MockCatalog(const std::string &root_path,
              const shash::Any  &catalog_hash,
              const uint64_t     catalog_size,
              const unsigned int revision,
              const time_t       last_modified,
              const bool         is_root,
              MockCatalog *parent   = NULL,
              MockCatalog *previous = NULL) :
    initialized_(false), parent_(parent), previous_(previous),
    root_path_(root_path), catalog_hash_(catalog_hash),
    catalog_size_(catalog_size), revision_(revision),
    last_modified_(last_modified), is_root_(is_root),
    owns_database_file_(false)
  {
    if (this->catalog_hash_.IsNull()) {
      this->catalog_hash_.Randomize();
    }
    if (parent != NULL) {
      parent->RegisterNestedCatalog(this);
    }
    unsigned pos = root_path.find_last_of('/');
    string parent_path = root_path.substr(0, pos);
    string name = root_path.substr(pos + 1, string::npos);
    File mountpoint_file(shash::Any(), 4096, parent_path, name);
    files_.push_back(mountpoint_file);
    atomic_inc32(&MockCatalog::instances);
  }

  MockCatalog(const MockCatalog &other) :
    initialized_(false), parent_(other.parent_), previous_(other.previous_),
    root_path_(other.root_path_), catalog_hash_(other.catalog_hash_),
    catalog_size_(other.catalog_size_), revision_(other.revision_),
    last_modified_(other.last_modified_), is_root_(other.is_root_),
    owns_database_file_(false), active_children_(other.active_children_),
    children_(other.children_), files_(other.files_),
    chunks_(other.chunks_)
  {
    atomic_inc32(&MockCatalog::instances);
  }

  ~MockCatalog() {
    atomic_dec32(&MockCatalog::instances);
  }

  /**
   * Adds a new catalog to the mounted children list
   * @param child new catalog-child to be added
   */
  void AddChild(MockCatalog *child);

  std::vector<MockCatalog*> GetChildren() const {
    std::vector<MockCatalog*> children_arr;
    for (unsigned i = 0; i < active_children_.size(); ++i)
      children_arr.push_back(active_children_[i].child);
    return children_arr;
  }
  bool HasParent() const { return parent_ != NULL; }
  /**
   * Removes a catalog from the already mounted catalog list
   * @param child catalog to be removed form the active catalog list
   */
  void RemoveChild(MockCatalog *child);
  catalog::InodeRange inode_range() const { return catalog::InodeRange(); }
  bool OpenDatabase(const std::string &db_path) {
    initialized_ = true;
    if (parent_ != NULL)
      parent_->AddChild(this);
    return true;
  }
  uint64_t max_row_id() const { return std::numeric_limits<uint64_t>::max(); }
  void set_inode_range(const catalog::InodeRange value) { }
  void SetInodeAnnotation(catalog::InodeAnnotation *new_annotation) { }
  void SetOwnerMaps(const catalog::OwnerMap *uid_map,
                    const catalog::OwnerMap *gid_map) { }
  bool IsInitialized() const { return initialized_; }
  MockCatalog* FindSubtree(const PathString &path);
  bool FindNested(const PathString &mountpoint,
                   shash::Any *hash, uint64_t *size) const;
  uint64_t GetTTL() const { return 0; }
  bool LookupRawSymlink(const PathString &path,
                                LinkString *raw_symlink) const { return false; }
  bool LookupPath(const PathString &path,
                  catalog::DirectoryEntry *dirent) const;
  bool ListingPath(const PathString &path,
                   catalog::DirectoryEntryList *listing,
                   const bool expand_symlink) const;

  bool GetVOMSAuthz(std::string *authz) { return false; }

  float schema() const { return 2.5; }

 protected:
  // silence coverity
  MockCatalog& operator= (const MockCatalog &other);

  // API in this 'public block' is used by CatalogTraversal
  // (see catalog.h - catalog::Catalog for details)
 public:
  static MockCatalog* AttachFreely(const std::string  &root_path,
                                   const std::string  &file,
                                   const shash::Any   &catalog_hash,
                                         MockCatalog  *parent      = NULL,
                                   const bool          is_not_root = false);

  bool IsRoot() const { return is_root_; }

  const NestedCatalogList& ListNestedCatalogs() const { return children_; }
  const NestedCatalogList  ListOwnNestedCatalogs() const { return children_; }
  const HashVector&        GetReferencedObjects() const;
  void TakeDatabaseFileOwnership() { owns_database_file_ = true;  }
  void DropDatabaseFileOwnership() { owns_database_file_ = false; }
  bool OwnsDatabaseFile() const    { return owns_database_file_;  }

  unsigned int GetRevision()     const { return revision_;      }
  uint64_t     GetLastModified() const { return last_modified_; }

  shash::Any GetPreviousRevision() const {
    return (previous_ != NULL) ? previous_->hash() : shash::Any();
  }

  const PathString   mountpoint()    const { return PathString(root_path_);  }
  const std::string& root_path()     const { return root_path_;              }
  const shash::Any&  hash()          const { return catalog_hash_;           }
  uint64_t           catalog_size()  const { return catalog_size_;           }
  unsigned int       revision()      const { return revision_;               }
  bool               volatile_flag() const { return false;                   }
  MockCatalog*       parent()        const { return parent_;                 }
  MockCatalog*       previous()      const { return previous_;               }

  std::string        database_path() const { return ""; }

  void set_parent(MockCatalog *parent) { parent_ = parent; }

 public:
  /**
   * Adds the catalog to the unmounted catalog list
   * @param child new catalog to be added to the unmounted catalog list
   */
  void RegisterNestedCatalog(MockCatalog *child);
  File *FindFile(std::string path) {
    shash::Md5 md5_path(path.c_str(), path.length());
    for (unsigned i = 0; i < files_.size(); ++i) {
      if (files_[i].path_hash == md5_path)
        return &files_[i];
    }
    return NULL;
  }
  void AddFile(const shash::Any &content_hash,
               const size_t file_size,
               const string &parent_path = "",
               const string &name = "");
  void AddChunk(const shash::Any &chunk_content_hash, const size_t chunk_size);

 protected:
  MockCatalog* Clone() const {
    return new MockCatalog(*this);
  }

 private:
  bool                initialized_;
  MockCatalog        *parent_;
  MockCatalog        *previous_;
  const std::string   root_path_;
  shash::Any          catalog_hash_;
  const uint64_t      catalog_size_;
  const unsigned int  revision_;
  const time_t        last_modified_;
  const bool          is_root_;
  bool                owns_database_file_;
  NestedCatalogList   active_children_;
  NestedCatalogList   children_;
  FileList            files_;
  ChunkList           chunks_;

  mutable HashVector  referenced_objects_;
};

//------------------------------------------------------------------------------

namespace catalog {

template <class CatalogMgrT>
class CatalogBalancer;

class MockCatalogManager : public AbstractCatalogManager<MockCatalog> {
 public:
  friend class CatalogBalancer<MockCatalogManager>;
  explicit MockCatalogManager(perf::Statistics *statistics) :
    AbstractCatalogManager<MockCatalog>(statistics), spooler_(new Spooler()),
    max_weight_(5), min_weight_(1), balance_weight_(3),
    autogenerated_catalogs_(0), num_added_files_(0) { }

  virtual ~MockCatalogManager() { delete spooler_; }

  virtual LoadError LoadCatalog(const PathString &mountpoint,
                                const shash::Any &hash,
                                std::string  *catalog_path,
                                shash::Any   *catalog_hash);

  virtual MockCatalog* CreateCatalog(const PathString  &mountpoint,
                                 const shash::Any  &catalog_hash,
                                 MockCatalog *parent_catalog);
  MockCatalog* RetrieveRootCatalog() { return GetRootCatalog(); }
  void AddFile(const DirectoryEntryBase &entry,
               const XattrList &xattrs,
               const std::string &parent_directory)
  {
    ++num_added_files_;
  }

  void RegisterNewCatalog(MockCatalog *new_catalog) {
    string mountpoint_str = new_catalog->root_path();
    PathString mountpoint(mountpoint_str.c_str(), mountpoint_str.length());
    catalog_map_[mountpoint] = new_catalog;
  }

  unsigned GetNumAutogeneratedCatalogs() { return autogenerated_catalogs_; }
  unsigned GetNumAddedFiles() { return num_added_files_; }

  MockCatalog *FindCatalog(const PathString &path) {
    map<PathString, MockCatalog*>::iterator it;
    string best_match = "";
    unsigned best_match_length = 0;
    for (it = catalog_map_.begin(); it != catalog_map_.end(); ++it) {
      string key = it->first.ToString();
      if (path.ToString().find(key) == 0) {
        unsigned length = key.length();
        if (length > best_match_length) {
          best_match_length = length;
          best_match = key;
        }
      }
    }
    return catalog_map_[PathString(best_match)];
  }

  /**
   * This method simply increases the number of autogenerated catalogs.
   * If this catalog is reused the counter would be wrong
   */
  void CreateNestedCatalog(
    const string &mountpoint)
  {
    ++autogenerated_catalogs_;
  }



 private:
  typedef struct {
    shash::Algorithms GetHashAlgorithm() { return shash::kShake128; }
  } Spooler;

  Spooler *spooler_;
  map<PathString, MockCatalog*> catalog_map_;
  unsigned max_weight_;
  unsigned min_weight_;
  unsigned balance_weight_;
  unsigned autogenerated_catalogs_;
  unsigned num_added_files_;
};

}  // namespace catalog


//------------------------------------------------------------------------------


class MockHistory : public history::History,
                    public MockObjectStorage<MockHistory> {
 public:
  typedef std::map<std::string, history::History::Tag> TagMap;
  typedef std::map<std::string, history::History::Branch> BranchMap;
  typedef std::set<shash::Any> HashSet;

  static const std::string rhs;
  static const shash::Any  root_hash;
  static unsigned int      instances;

  using MockObjectStorage<MockHistory>::Exists;

 public:
  static void ResetGlobalState();

  static MockHistory* Open(const std::string &path);
  MockHistory(const bool          writable,
              const std::string  &fqrn);
  MockHistory(const MockHistory &other);
  ~MockHistory();

 protected:
  // silence coverity
  MockHistory& operator= (const MockHistory &other);

 public:
  MockHistory* Open() const;
  MockHistory* Clone(const bool writable = false) const;

  bool IsWritable()          const { return writable_;    }
  unsigned GetNumberOfTags() const { return tags_.size(); }
  bool BeginTransaction()    const { return true;         }
  bool CommitTransaction()   const { return true;         }

  bool SetPreviousRevision(const shash::Any &history_hash) {
    previous_revision_ = history_hash;
    return true;
  }
  shash::Any previous_revision() const { return previous_revision_; }

  bool Insert(const Tag &tag);
  bool Remove(const std::string &name);
  bool Exists(const std::string &name) const;
  bool GetByName(const std::string &name, Tag *tag) const;
  bool GetByDate(const time_t timestamp, Tag *tag) const;
  bool List(std::vector<Tag> *tags) const;

  bool GetBranchHead(const std::string &branch_name, Tag *tag) const;
  bool InsertBranch(const Branch &branch);
  bool PruneBranches();
  bool ListBranches(std::vector<Branch> *branches) const;
  bool ExistsBranch(const std::string &branch_name) const;

  bool ListRecycleBin(std::vector<shash::Any> *hashes) const;
  bool EmptyRecycleBin();

  bool Rollback(const Tag &updated_target_tag);
  bool ListTagsAffectedByRollback(const std::string  &target_tag_name,
                                  std::vector<Tag>   *tags) const;

  bool GetHashes(std::vector<shash::Any> *hashes) const;

  bool Vacuum() { return true; }
  void TakeDatabaseFileOwnership() { owns_database_file_ = true;  }
  void DropDatabaseFileOwnership() { owns_database_file_ = false; }
  bool OwnsDatabaseFile() const    { return owns_database_file_;  }

 public:
  void set_writable(const bool writable) { writable_ = writable; }

 protected:
  void GetTags(std::vector<Tag> *tags) const;

 private:
  static const Tag& get_tag(const TagMap::value_type &itr) {
    return itr.second;
  }

  static const shash::Any& get_hash(const Tag &tag) {
    return tag.root_hash;
  }

  static bool gt_hashes(const Tag &lhs, const Tag &rhs) {
    return lhs.root_hash > rhs.root_hash;
  }

  static bool eq_hashes(const Tag &lhs, const Tag &rhs) {
    return lhs.root_hash == rhs.root_hash;
  }

  struct DateSmallerThan {
    explicit DateSmallerThan(time_t date) : date_(date) { }
    bool operator()(const Tag &tag) const {
      return tag.timestamp <= date_;
    }
    const time_t date_;
  };

  struct RollbackPredicate {
    explicit RollbackPredicate(const Tag &tag, const bool inverse = false)
      : tag_(tag)
      , inverse_(inverse)
    { }
    bool operator()(const Tag &tag) const {
      const bool p = (tag.revision > tag_.revision || tag.name == tag_.name);
      return inverse_ ^ p;
    }
    const Tag  &tag_;
    const bool  inverse_;
  };

  struct TagRemover {
    explicit TagRemover(MockHistory *history) : history_(history) { }
    bool operator()(const Tag &tag) {
      return history_->Remove(tag.name);
    }
    MockHistory *history_;
  };

 private:
  TagMap      tags_;
  BranchMap   branches_;
  HashSet     recycle_bin_;
  bool        writable_;
  shash::Any  previous_revision_;
  bool        owns_database_file_;
};


//------------------------------------------------------------------------------


class MockReflog : public MockObjectStorage<MockReflog> {
 protected:
  MockReflog* Clone() const;

 public:
  static MockReflog* Open(const std::string &path);
  static MockReflog* Create(const std::string &path,
                            const std::string &repo_name);
  static void HashDatabase(const std::string &database_path,
                           shash::Any *hash_reflog);
  static void ResetGlobalState() {}

  bool AddCertificate(const shash::Any &certificate);
  bool AddCatalog(const shash::Any &catalog);
  bool AddHistory(const shash::Any &history);
  bool AddMetainfo(const shash::Any &metainfo);

  uint64_t CountEntries() { return references_.size(); }
  bool List(SqlReflog::ReferenceType type,
            std::vector<shash::Any> *hashes) const;
  bool ListOlderThan(SqlReflog::ReferenceType type,
                     uint64_t timestamp,
                     std::vector<shash::Any> *hashes) const;

  bool GetCatalogTimestamp(const shash::Any &catalog, uint64_t *timestamp);

  bool Remove(const shash::Any &hash);

  bool ContainsCertificate(const shash::Any &certificate) const;
  bool ContainsCatalog(const shash::Any &catalog) const;
  bool ContainsHistory(const shash::Any &history) const;
  bool ContainsMetainfo(const shash::Any &metainfo) const;

  void TakeDatabaseFileOwnership() { owns_database_file_ = true;  }
  void DropDatabaseFileOwnership() { owns_database_file_ = false; }
  bool OwnsDatabaseFile() const    { return owns_database_file_;  }

  std::string fqrn() const { return fqrn_; }

 protected:
  explicit MockReflog(const std::string fqrn);

 private:
  bool                  owns_database_file_;
  const std::string     fqrn_;
  std::map<shash::Any, uint64_t>  references_;
};


//------------------------------------------------------------------------------


class MockObjectFetcher;

template <>
struct object_fetcher_traits<MockObjectFetcher> {
  typedef MockCatalog CatalogTN;
  typedef MockHistory HistoryTN;
  typedef MockReflog  ReflogTN;
};

/**
 * This is a mock of an ObjectFetcher that does essentially nothing.
 */
class MockObjectFetcher : public AbstractObjectFetcher<MockObjectFetcher> {
 public:
  typedef AbstractObjectFetcher<MockObjectFetcher>::Failures Failures;
  typedef AbstractObjectFetcher<MockObjectFetcher>           BaseTN;

 public:
  MockObjectFetcher() : BaseTN("") {}

  using BaseTN::FetchManifest;  // un-hiding convenience overload
  Failures FetchManifest(manifest::Manifest** manifest);
  Failures Fetch(const shash::Any &object_hash, std::string *file_path);
  Failures Fetch(const std::string &relative_path,
                 const bool         decompress,
                 const bool         nocache,
                       std::string *file_path);

 private:
  bool ObjectExists(const shash::Any &object_hash) const;
  bool PathExists(const std::string &path) const;
};


#endif  // TEST_UNITTESTS_TESTUTIL_H_
