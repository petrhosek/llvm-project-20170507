//===--- Headers.h - Include headers -----------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_HEADERS_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_HEADERS_H

#include "Path.h"
#include "Protocol.h"
#include "SourceCode.h"
#include "clang/Basic/VirtualFileSystem.h"
#include "clang/Format/Format.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Tooling/Inclusions/HeaderIncludes.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Error.h"

namespace clang {
namespace clangd {

/// Returns true if \p Include is literal include like "path" or <path>.
bool isLiteralInclude(llvm::StringRef Include);

/// Represents a header file to be #include'd.
struct HeaderFile {
  std::string File;
  /// If this is true, `File` is a literal string quoted with <> or "" that
  /// can be #included directly; otherwise, `File` is an absolute file path.
  bool Verbatim;

  bool valid() const;
};

// An #include directive that we found in the main file.
struct Inclusion {
  Range R;             // Inclusion range.
  std::string Written; // Inclusion name as written e.g. <vector>.
  Path Resolved;       // Resolved path of included file. Empty if not resolved.
};

// Information captured about the inclusion graph in a translation unit.
// This includes detailed information about the direct #includes, and summary
// information about all transitive includes.
//
// It should be built incrementally with collectIncludeStructureCallback().
// When we build the preamble, we capture and store its include structure along
// with the preamble data. When we use the preamble, we can copy its
// IncludeStructure and use another collectIncludeStructureCallback() to fill
// in any non-preamble inclusions.
class IncludeStructure {
public:
  std::vector<Inclusion> MainFileIncludes;

  // Return all transitively reachable files, and their minimum include depth.
  // All transitive includes (absolute paths), with their minimum include depth.
  // Root --> 0, #included file --> 1, etc.
  // Root is clang's name for a file, which may not be absolute.
  // Usually it should be SM.getFileEntryForID(SM.getMainFileID())->getName().
  llvm::StringMap<unsigned> includeDepth(llvm::StringRef Root) const;

  // This updates IncludeDepth(), but not MainFileIncludes.
  void recordInclude(llvm::StringRef IncludingName,
                     llvm::StringRef IncludedName,
                     llvm::StringRef IncludedRealName);

private:
  // Identifying files in a way that persists from preamble build to subsequent
  // builds is surprisingly hard. FileID is unavailable in InclusionDirective(),
  // and RealPathName and UniqueID are not preseved in the preamble.
  // We use the FileEntry::Name, which is stable, interned into a "file index".
  // The paths we want to expose are the RealPathName, so store those too.
  std::vector<std::string> RealPathNames; // In file index order.
  unsigned fileIndex(llvm::StringRef Name);
  llvm::StringMap<unsigned> NameToIndex; // Values are file indexes.
  // Maps a file's index to that of the files it includes.
  llvm::DenseMap<unsigned, SmallVector<unsigned, 8>> IncludeChildren;
};

/// Returns a PPCallback that visits all inclusions in the main file.
std::unique_ptr<PPCallbacks>
collectIncludeStructureCallback(const SourceManager &SM, IncludeStructure *Out);

// Calculates insertion edit for including a new header in a file.
class IncludeInserter {
public:
  IncludeInserter(StringRef FileName, StringRef Code,
                  const format::FormatStyle &Style, StringRef BuildDir,
                  HeaderSearch &HeaderSearchInfo)
      : FileName(FileName), Code(Code), BuildDir(BuildDir),
        HeaderSearchInfo(HeaderSearchInfo),
        Inserter(FileName, Code, Style.IncludeStyle) {}

  void addExisting(const Inclusion &Inc);

  /// Checks whether to add an #include of the header into \p File.
  /// An #include will not be added if:
  ///   - Either \p DeclaringHeader or \p InsertedHeader is already (directly)
  ///   in \p Inclusions (including those included via different paths).
  ///   - \p DeclaringHeader or \p InsertedHeader is the same as \p File.
  ///
  /// \param DeclaringHeader is the original header corresponding to \p
  /// InsertedHeader e.g. the header that declares a symbol.
  /// \param InsertedHeader The preferred header to be inserted. This could be
  /// the same as DeclaringHeader but must be provided.
  bool shouldInsertInclude(const HeaderFile &DeclaringHeader,
                           const HeaderFile &InsertedHeader) const;

  /// Determines the preferred way to #include a file, taking into account the
  /// search path. Usually this will prefer a shorter representation like
  /// 'Foo/Bar.h' over a longer one like 'Baz/include/Foo/Bar.h'.
  ///
  /// \param DeclaringHeader is the original header corresponding to \p
  /// InsertedHeader e.g. the header that declares a symbol.
  /// \param InsertedHeader The preferred header to be inserted. This could be
  /// the same as DeclaringHeader but must be provided.
  ///
  /// \return A quoted "path" or <path> to be included.
  std::string calculateIncludePath(const HeaderFile &DeclaringHeader,
                                   const HeaderFile &InsertedHeader) const;

  /// Calculates an edit that inserts \p VerbatimHeader into code. If the header
  /// is already included, this returns None.
  llvm::Optional<TextEdit> insert(llvm::StringRef VerbatimHeader) const;

private:
  StringRef FileName;
  StringRef Code;
  StringRef BuildDir;
  HeaderSearch &HeaderSearchInfo;
  llvm::StringSet<> IncludedHeaders; // Both written and resolved.
  tooling::HeaderIncludes Inserter;  // Computers insertion replacement.
};

} // namespace clangd
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANGD_HEADERS_H
