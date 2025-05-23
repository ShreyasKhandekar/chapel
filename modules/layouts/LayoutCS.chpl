/*
 * Copyright 2020-2025 Hewlett Packard Enterprise Development LP
 * Copyright 2004-2019 Cray Inc.
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Draft support for storing sparse 2D domains/arrays using CSR/CSC layouts. */

@deprecated("'LayoutCS' and its 'CS' layout are deprecated; please use ':mod:`CompressedSparseLayout`' and its 'csrLayout' or 'cscLayout' layouts instead")
prototype module LayoutCS {
import Sort.{keyComparator};
import RangeChunk;

@chpldoc.nodoc
/* Debug flag */
config param debugCS = false;

@chpldoc.nodoc
config param csLayoutSupportsAutoLocalAccess = true;

/* Default sparse dimension index sorting mode for LayoutCS.
Sparse dimension indices will default to sorted order if true, inserted order if false */
config param LayoutCSDefaultToSorted = true;

@chpldoc.nodoc
/* Comparator used for sorting by columns */
record _ColumnComparator: keyComparator {
  proc key(idx: _tuple) { return (idx(1), idx(0));}
}

@chpldoc.nodoc
const _columnComparator: _ColumnComparator;


//
// Necessary since `t == CS` does not support classes with param fields
//
@chpldoc.nodoc
proc isCSType(type t) param do return isSubtype(_to_borrowed(t), CS);

/*
This CS layout provides a Compressed Sparse Row (CSR) and Compressed Sparse
Column (CSC) implementation for Chapel's sparse domains and arrays.

To declare a CS domain, invoke the ``CS`` initializer in a `dmapped` clause,
specifying CSR vs. CSC format with the ``param compressRows`` argument, which
defaults to ``true`` if omitted. For example:

  .. code-block:: chapel

    use LayoutCS;
    var D = {0..#n, 0..#m};  // a default-distributed domain
    var CSR_Domain: sparse subdomain(D) dmapped new dmap(new CS(compressRows=true)); // Default argument
    var CSC_Domain : sparse subdomain(D) dmapped new dmap(new CS(compressRows=false));


To declare a CSR or CSC array, use a CSR or CSC domain, respectively.
For example:

  .. code-block:: chapel

    // assumes the above declarations
    var CSR_Array: [CSR_Domain] real;
    var CSC_Array: [CSC_Domain] real;

This domain map is a layout, i.e. it maps all indices to the current locale.
All elements of a CS-distributed array are stored
on the locale where the array variable is declared.  By default, the CS
layout stores sparse indices in sorted order.  However, this default can
be changed for a program by compiling with ``-sLayoutCSDefaultToSorted=false``,
or for a specific domain by passing ``sortedIndices=false`` as an argument
to the ``CS()`` initializer.
*/
@deprecated("'CS' is deprecated, please use 'CompressedSparseLayout.[csrLayout|cscLayout]' instead")
class CS: BaseDist {
  param compressRows: bool = true;
  param sortedIndices: bool = LayoutCSDefaultToSorted;

  proc init(param compressRows: bool = true,
            param sortedIndices: bool = LayoutCSDefaultToSorted) {
    this.compressRows = compressRows;
    this.sortedIndices = sortedIndices;
  }

  override proc dsiNewSparseDom(param rank: int, type idxType, dom: domain) {
    return new unmanaged CSDom(rank, idxType, this.compressRows, this.sortedIndices, dom.strides, _to_unmanaged(this), dom);
  }

  proc dsiClone() {
    return new unmanaged CS(compressRows=this.compressRows,sortedIndices=this.sortedIndices);
  }

  proc dsiEqualDMaps(that: CS(this.compressRows,this.sortedIndices)) param {
    return true;
  }

  proc dsiEqualDMaps(that) param {
    return false;
  }

  override proc dsiIsLayout() param {
    return true;
  }
} // CS


class CSDom: BaseSparseDomImpl(?) {
  param compressRows;
  param sortedIndices;
  param strides;
  var dist: unmanaged CS(compressRows,sortedIndices);

  var rowRange: range(idxType, strides=strides);
  var colRange: range(idxType, strides=strides);

  /* (row|col) startIdxDom */
  var startIdxDom: domain(1, idxType);

  var _nnz = 0;

  /* (row|col) start */
  pragma "local field"
  var startIdx: [startIdxDom] idxType;    // would like index(nnzDom)
  /* (row|col) idx */
  pragma "local field"
  var idx: [nnzDom] idxType;      // would like index(parentDom.dim(0))

  /* Initializer */
  proc init(param rank, type idxType, param compressRows, param sortedIndices, param strides, dist: unmanaged CS(compressRows,sortedIndices), parentDom: domain) {
    if (rank != 2 || parentDom.rank != 2) then
      compilerError("Only 2D sparse domains are supported by the CS distribution");
    if parentDom.idxType != idxType then
      compilerError("idxType mismatch in CSDom.init(): " + idxType:string + " != " + parentDom.idxType:string);

    super.init(rank, idxType, parentDom);

    this.compressRows = compressRows;
    this.sortedIndices = sortedIndices;
    this.strides      = strides;

    this.dist = dist;
    rowRange = parentDom.dim(0);
    colRange = parentDom.dim(1);
    // startIdx array takes advantage of potentially using a non-zero-indexed bounding box
    startIdxDom = if compressRows
                  then {rowRange.lowBound..rowRange.highBound+1}
                  else {colRange.lowBound..colRange.highBound+1};

    init this;

    nnzDom = {0..#_nnz};
    dsiClear();
  }

  override proc getNNZ(): int {
    return _nnz;
  }
  override proc dsiMyDist() do return dist;

  proc dsiAssignDomain(rhs: domain, lhsPrivate:bool) {
    if _to_borrowed(rhs._instance.type) == this.type &&
       canDoDirectAssignment(rhs) {
      // Optimized CSC->CSC / CSR->CSR

      // ENGIN: We cannot use bulkGrow here, because rhs might be grown using
      // grow, which has a different heuristic to grow the internal arrays.
      // That may result in size mismatch in the following internal array
      // assignments
      this._nnz = rhs._nnz;
      this.nnzDom = rhs.nnzDom;

      this.startIdx = rhs.startIdx;
      this.idx = rhs.idx;
    } else if isProperSubtype(_to_borrowed(rhs._instance.type), DefaultSparseDom) {
      // Optimized COO -> CSR/CSC

      // Note: only COO->CSR can take advantage of COO having sorted indices
      this.dsiBulkAdd(
          rhs._instance._indices[rhs.nnzDom.lowBound..#rhs._nnz],
          dataSorted=this.compressRows, isUnique=true);
    } else {
      // Unoptimized generic case
      chpl_assignDomainWithIndsIterSafeForRemoving(this, rhs);
    }
  }

  proc dsiBuildArray(type eltType, param initElts: bool) {
    return new unmanaged CSArr(eltType=eltType, rank=rank, idxType=idxType,
                               dom=_to_unmanaged(this),
                               initElts=initElts);
  }

  iter dsiIndsIterSafeForRemoving() {
    var cursor = if this.compressRows then rowRange.highBound else colRange.highBound;
    for i in 0..#_nnz by -1 {
      while (startIdx(cursor) > i) {
        cursor -= 1;
      }
      if this.compressRows {
        yield (cursor, idx(i));
      } else {
        yield (idx(i), cursor);
      }
    }
  }

  iter these() {
    // TODO: Is it faster to start at _private_findStart(1) ?
    var cursor = if this.compressRows then rowRange.lowBound else colRange.lowBound;
    for i in 0..#_nnz {
      while (startIdx(cursor+1) <= i) {
        cursor+= 1;
      }
      if this.compressRows then
        yield (cursor, idx(i));
      else
        yield (idx(i), cursor);
    }
  }

  iter these(param tag: iterKind) where tag == iterKind.leader {
    use DSIUtil;
    // same as DefaultSparseDom's leader
    const numElems = _nnz;
    const numChunks = _computeNumChunks(numElems);
    if debugCS then
      writeln("CSDom leader: ", numChunks, " chunks, ", numElems, " elems");

    // split our numElems elements over numChunks tasks
    if numChunks == 1 then
      yield (this, 0, numElems-1);
    else
      coforall chunk in RangeChunk.chunks(0..#numElems, numChunks) do
        yield (this, chunk.first, chunk.last);
    // TODO: to handle large numElems and numChunks faster, it would be great
    // to run the binary search in _private_findStart smarter, e.g.
    // pass to the tasks created in 'coforall' smaller ranges to search over.
  }

  iter these(param tag: iterKind, followThis: (?,?,?)) where tag == iterKind.follower {
    var (followThisDom, startIx, endIx) = followThis;

    if boundsChecking then
      assert(startIx <= endIx, "CSDom follower - got nothing to iterate over");

    // TODO: Allow zippering sparse domains of equal length some day...
    if (followThisDom != this) then
      halt("Sparse domains can't be zippered with anything other than themselves and their arrays (CS layout)");

    // This loop is identical to the serial iterator, except for the iteration
    // space and finding the initial 'cursorRow'.
    var cursor = _private_findStart(startIx);
    if debugCS then
      writeln("CSDom follower: ", startIx, "..", endIx,
              "  startIdx(", cursor, ")=", startIdx(cursor));

    for i in startIx..endIx {
      while (startIdx(cursor+1) <= i) do cursor+= 1;
      if this.compressRows then
        yield (cursor, idx(i));
      else
        yield (idx(i), cursor);
    }
  }

  iter these(param tag: iterKind, followThis) where tag == iterKind.follower {
    compilerError("Sparse iterators can't yet be zippered with others (CS layout)");
    yield 0;    // Dummy.
  }

  // Helper: find 'ix' s.t. startIdx(ix) <= startIx < startIdx(ix+1)
  // or a number at most 'approx' smaller than that.
  // There MUST exist a solution within low..high.
  proc _private_findStart(startIx) {
    return _private_findStart(startIx, startIdxDom.lowBound, startIdxDom.highBound);
  }
  proc _private_findStart(startIx, low, high) {
    var approx = 2; // Indicates when to switch to linear search.
                    // This number could be tuned for performance.
    // simple binary search (should be fewer comparisons than binarySearch())
    var l = low, h = high;
    while h > l + approx {
      var m = (h + l) / 2;
      if startIdx(m) <= startIx then l = m; else h = m;
    }
    var hh = min(l+approx, high);

    if boundsChecking then {
      assert(startIdx(l) <= startIx, "CSDom.findStart-1");
      assert(startIx < startIdx(hh), "CSDom.findStart-2");
      // The second assertion will fail if there is NO solution
      // within low..high.  For performance,
      // ensuring existence of a solution is caller responsibility.
    }
    return l;
  }

  proc stopIdx(i) {
    return startIdx(i+1)-1;
  }

  proc find(ind: rank*idxType) {
    use Search;

    const (row, col) = ind;

    var ret: (bool, idxType);
    if this.compressRows {
      if this.sortedIndices then
        ret = binarySearch(idx, col, lo=startIdx(row), hi=stopIdx(row));
      else
        ret = linearSearch(idx, col, lo=startIdx(row), hi=stopIdx(row));
    } else {
      if this.sortedIndices then
        ret = binarySearch(idx, row, lo=startIdx(col), hi=stopIdx(col));
      else
        ret = linearSearch(idx, row, lo=startIdx(col), hi=stopIdx(col));
    }

    return ret;
  }

  proc dsiMember(ind: rank*idxType) {
    if parentDom.contains(ind) {
      const (found, loc) = find(ind);
      return found;
    }
    return false;
  }

  override proc dsiFirst {
    if _nnz == 0 then return (parentDom.lowBound) - (1,1);
    // find the corresponding index into startIdx
    for i in startIdxDom {
      if startIdx[i] > idx.domain.lowBound {
        if this.compressRows then
          return (i-1, idx[idx.domain.lowBound]);
        else
          return (idx[idx.domain.lowBound], i-1);
      }
    }
    halt("Something went wrong in dsiFirst");
  }

  override proc dsiLast {
    if _nnz == 0 then return (parentDom.lowBound) - (1,1);

    const _lastIdx = _nnz-1;
    var _lastMinor = idx[_lastIdx];
    var _lastMajor = parentDom.lowBound[1] - 1;

    for i in startIdxDom.lowBound..startIdxDom.highBound by -1 {
        if startIdx[i] > _lastIdx then _lastMajor = i-1;
    }
    if this.compressRows then
      return (_lastMajor, _lastMinor);
    else
      return (_lastMinor, _lastMajor);
  }

  proc dsiAdd(ind: rank*idxType) {
    boundsCheck(ind);

    // find position in nnzDom to insert new index
    const (found, insertPt) = find(ind);

    // if the index already existed, then return
    if found then return 0;

    // increment number of nonzeroes
    _nnz += 1;

    // double nnzDom if we've outgrown it; grab current size otherwise
    var oldNNZDomSize = nnzDom.size;
    _grow(_nnz);

    const (row,col) = ind;

    // shift row|column indices up
    for i in insertPt.._nnz-2 by -1 {
      idx(i+1) = idx(i);
    }

    if this.compressRows then
      idx(insertPt) = col;
    else
      idx(insertPt) = row;

    // bump the startIdx counts
    var start = if this.compressRows then row else col;

    for rc in start+1..startIdxDom.highBound {  // want startIdxDom[row+1..]
      startIdx(rc) += 1;
    }

    // shift all of the arrays up and initialize nonzeroes if
    // necessary
    //
    // BLC: Note: if rectangular arrays had a user-settable
    // initialization value, we could set it to be the IRV and skip
    // this second initialization of any new values in the array.
    // we could also eliminate the oldNNZDomSize variable
    for a in _arrs {
      a.sparseShiftArray(insertPt.._nnz-2, oldNNZDomSize..nnzDom.size-1);
    }
    return 1;
  }

  override proc bulkAdd_help(ref inds: [?indsDom] rank*idxType,
      dataSorted=false, isUnique=false, addOn=nilLocale) {
    import Sort;

    if addOn != nilLocale {
      if addOn != this.locale {
        halt("Bulk index addition is only possible on the locale where the\
            sparse domain is created");
      }
    }

    if this.compressRows then
      bulkAdd_prepareInds(inds, dataSorted, isUnique,
                          cmp = new Sort.defaultComparator());
    else
      bulkAdd_prepareInds(inds, dataSorted, isUnique, cmp=_columnComparator);

    if _nnz == 0 {

      const dupCount = if isUnique then 0 else _countDuplicates(inds);

      _nnz += inds.size-dupCount;
      _bulkGrow();

      var idxIdx = 0;
      var prevIdx = parentDom.lowBound - (1,1);
      var current: idxType;

      if this.compressRows {
        current = parentDom.dim(0).lowBound;
      } else {
        current = parentDom.dim(1).lowBound;
      }

      // Update startIdx && idx
      for (i,j) in inds {
        if !isUnique && (i,j) == prevIdx then continue;
        else prevIdx = (i,j);

        if this.compressRows {
          while i != current {
            current += 1;
            startIdx[current+1] = startIdx[current];
          }
          startIdx[i+1] += 1;
          idx[idxIdx] = j;
          idxIdx += 1;
        } else {
          while j != current {
            current += 1;
            startIdx[current+1] = startIdx[current];
          }
          startIdx[j+1] += 1;
          idx[idxIdx] = i;
          idxIdx += 1;
        }
      }


      // make sure startIdx[i]>startIdx[j] for i>j for possibly
      // untouched part of startIdx
      const startIdxHigh = startIdx[current+1];
      for r in current+2..startIdx.domain.highBound {
        startIdx[r] = startIdxHigh;
      }

      return idxIdx;
    } // if _nnz == 0

    const (actualInsertPts, actualAddCnt) =
      __getActualInsertPts(this, inds, isUnique);
    const oldnnz = _nnz;
    _nnz += actualAddCnt;

    // Grow nnzDom if necessary
    _bulkGrow();

    // Linearly fill the new idx from backwards
    var newIndIdx = indsDom.highBound; //index into new indices
    var oldIndIdx = oldnnz-1; //index into old indices
    var newLoc = actualInsertPts[newIndIdx]; // its position-to-be in new dom
    while newLoc == -1 {
      newIndIdx -= 1;
      if newIndIdx == indsDom.lowBound-1 then break; // there were duplicates -- now done
      newLoc = actualInsertPts[newIndIdx];
    }

    var arrShiftMap: [{0..#oldnnz}] int; //to map where data goes
    for i in 0..#_nnz by -1 {
      if oldIndIdx >= 0 && i > newLoc {
        // Shift from old values
        idx[i] = idx[oldIndIdx];
        arrShiftMap[oldIndIdx] = i;
        oldIndIdx -= 1;
      }
      else if newIndIdx >= indsDom.lowBound && i == newLoc {
        // Put the new nonzero in
        if this.compressRows {
          idx[i] = inds[newIndIdx][1];
        } else {
          idx[i] = inds[newIndIdx][0];
        }
        newIndIdx -= 1;
        if newIndIdx >= indsDom.lowBound then
          newLoc = actualInsertPts[newIndIdx];
        else
          newLoc = -2; // Finished new set
        while newLoc == -1 {
          newIndIdx -= 1;
          if newIndIdx == indsDom.lowBound-1 then break; // There were duplicates -- now done
          newLoc = actualInsertPts[newIndIdx];
        }
      }
      else halt("Something went wrong");
    }

    // Aggregated row || col shift
    var prevCursor = if this.compressRows then parentDom.dim(0).lowBound else parentDom.dim(1).lowBound;
    var cursor: int;
    var cursorCnt = 0;
    for (ind, p) in zip(inds, actualInsertPts)  {
      if p == -1 then continue;
      if this.compressRows then
        cursor = ind[0];
      else
        cursor = ind[1];
      if cursor == prevCursor then cursorCnt += 1;
      else {
        startIdx[prevCursor+1] += cursorCnt;
        if cursor - prevCursor > 1 {
          for i in prevCursor+2..cursor {
            startIdx[i] += cursorCnt;
          }
        }
        cursorCnt += 1;
        prevCursor = cursor;
      }
    }
    for i in prevCursor+1..startIdxDom.highBound {
      startIdx[i] += cursorCnt;
    }

    for a in _arrs do
      a.sparseBulkShiftArray(arrShiftMap, oldnnz);

    return actualAddCnt;
  }

  proc dsiRemove(ind: rank*idxType) {
    // find position in nnzDom to remove old index
    const (found, insertPt) = find(ind);

    // if the index doesn't already exist, then return
    if (!found) then return 0;

    // increment number of nonzeroes
    _nnz -= 1;

    _shrink(_nnz);

    const (row,col) = ind;

    // shift column indices down
    for i in insertPt.._nnz-1 {
      idx(i) = idx(i+1);
    }

    // bump the startIdx counts
    if this.compressRows {
      for r in row+1..startIdxDom.highBound {  // want startIdxDom[row+1..]
        startIdx(r) -= 1;
      }
    } else {
      for r in col+1..startIdxDom.highBound {  // want startIdxDom[row+1..]
        startIdx(r) -= 1;
      }
    }

    // shift all of the arrays up and initialize nonzeroes if
    // necessary
    //
    // BLC: Note: if rectangular arrays had a user-settable
    // initialization value, we could set it to be the IRV and skip
    // this second initialization of any new values in the array.
    // we could also eliminate the oldNNZDomSize variable
    for a in _arrs {
      a.sparseShiftArrayBack(insertPt.._nnz-1);
    }

    return 1;
  }

  override proc dsiClear() {
    _nnz = 0;
    startIdx = 0;
  }

  iter dimIter(param d, ind) {
    if (d != 1 && this.compressRows) {
      compilerError("dimIter(0, ..) not supported on CS(compressRows=true) domains");
    } else if (d != 0 && !this.compressRows) {
      compilerError("dimIter(1, ..) not supported on CS(compressRows=false) domains");
    }

    foreach i in startIdx[ind]..stopIdx[ind] do
      yield idx[i];
  }

  proc dsiSerialWrite(f) {
    f.write("{\n");
    if this.compressRows {
      for r in rowRange {
        const lo = startIdx(r),
              hi = stopIdx(r);
        for c in lo..hi {
          f.write(" (", r, ", ", idx(c), ")");
          if c == hi then f.write("\n");
        }
      }
    } else {
      // Note: Prints differently than CSR
      for c in colRange {
        const lo = startIdx(c),
              hi = stopIdx(c);
        for r in lo..hi {
          f.write(" (", idx(r), ", ", c, ")");
          if r == hi then f.write("\n");
        }
      }
    }
    f.write("}\n");
  }

  override proc dsiSupportsAutoLocalAccess() param {
    return csLayoutSupportsAutoLocalAccess;
  }

} // CSDom


proc CSDom.rows() {
  return this.rowRange;
}

proc CSDom.cols() {
  return this.colRange;
}

@chpldoc.nodoc
iter CSDom.uidsInRowCol(rc) {
  for uid in startIdx[rc]..<startIdx[rc+1] do
    yield uid;
}


class CSArr: BaseSparseArrImpl(?) {

  proc init(type eltType,
            param rank : int,
            type idxType,
            dom,
            param initElts:bool) {
    super.init(eltType, rank, idxType, dom, initElts);
  }

  // dsiDestroyArr is defined in BaseSparseArrImpl

  proc dsiAccess(ind: rank*idxType) ref {
    // make sure we're in the dense bounding box
    dom.boundsCheck(ind);

    // lookup the index and return the data or IRV
    const (found, loc) = dom.find(ind);
    if found then
      return data(loc);
    else
      halt("attempting to assign a 'zero' value in a sparse array at index ",
           if rank == 1 then ind(0) else ind);
  }
  // value version for POD types
  proc dsiAccess(ind: rank*idxType)
  where shouldReturnRvalueByValue(eltType) {
    // make sure we're in the dense bounding box
    dom.boundsCheck(ind);

    // lookup the index and return the data or IRV
    const (found, loc) = dom.find(ind);
    if found then
      return data(loc);
    else
      return irv;
  }
  // const ref version for types with copy ctors
  proc dsiAccess(ind: rank*idxType) const ref {
    // make sure we're in the dense bounding box
    dom.boundsCheck(ind);

    // lookup the index and return the data or IRV
    const (found, loc) = dom.find(ind);
    if found then
      return data(loc);
    else
      return irv;
  }



  iter these() ref {
    foreach i in 0..#dom._nnz do yield data[i];
  }

  iter these(param tag: iterKind) where tag == iterKind.leader {
    // forward to the leader iterator on our domain
    // Note: this is so that arrays can be zippered with domains;
    // otherwise just chunk up data[1..dom._nnz] a-la DefaultSparseArr.
    for followThis in dom.these(tag) do
      yield followThis;
  }

  iter these(param tag: iterKind, followThis: (?,?,?)) ref where tag == iterKind.follower {
    // simpler than CSDom's follower - no need to deal with rows (or columns)
    var (followThisDom, startIx, endIx) = followThis;

    if (followThisDom != this.dom) then
      halt("Sparse arrays can't be zippered with anything other than their domains and sibling arrays (CS layout)");
    if debugCS then
      writeln("CSArr follower: ", startIx, "..", endIx);

    foreach i in startIx..endIx do yield data[i];
  }

  iter these(param tag: iterKind, followThis) where tag == iterKind.follower {
    compilerError("Sparse iterators can't yet be zippered with others (CS layout)");
    yield 0;    // Dummy.
  }

  proc dsiSerialWrite(f) {
    if dom.compressRows {
      for r in dom.rowRange {
        const lo = dom.startIdx(r);
        const hi = dom.stopIdx(r);
        for c in lo..hi {
          f.write(data(c), if (c==hi) then "\n" else " ");
        }
      }
    } else {
      for c in dom.colRange {
        const lo = dom.startIdx(c);
        const hi = dom.stopIdx(c);
        for r in lo..hi {
          f.write(data(r), if (r==hi) then "\n" else " ");
        }
      }
    }
  }

  proc doiBulkTransferToKnown(srcDom, destClass: this.type, destDom) {
    if srcDom == destDom {
      destClass.data = this.data;
      return true;
    } else {
      return false;
    }
  }

  proc doiBulkTransferFromKnown(destDom, srcClass: this.type, srcDom): bool {
    if srcDom == destDom {
      this.data = srcClass.data;
      return true;
    } else {
      return false;
    }
  }
} // CSArr

proc CSArr.rows() {
  return this.dom.rows();
}

proc CSArr.cols() {
  return this.dom.cols();
}

@chpldoc.nodoc
iter CSArr.indsAndVals(rc) {
  ref dom = this.dom;
  for uid in dom.uidsInRowCol(rc) do
    yield (dom.idx[uid], this.data[uid]);
}

iter CSArr.colsAndVals(r) {
  if this.dom.compressRows == false then
    compilerError("Can't (efficiently) iterate over rows using a CSC layout");
  for colVal in indsAndVals(r) do
    yield colVal;
}

iter CSArr.rowsAndVals(c) {
  if this.dom.compressRows == true then
    compilerError("Can't (efficiently) iterate over columns using a CSR layout");
  for rowVal in indsAndVals(c) do
    yield rowVal;
}

} // LayoutCS
