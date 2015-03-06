/* 
The MIT License (MIT)
Copyright (c) 2014 Intel Corp.
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef _FAST_EXTEND_VEC64_H
#define _FAST_EXTEND_VEC64_H

#include "fast_extend_vec.h"
#include "fast_extend_bitv64.h"

//
// Edit distance vector for a 64-bit query.
// 16-bit words will be stored in an SSE vector in interleaved order as follows:
//           w7 w5 w3 w1 w6 w4 w2 w0
//
class EDVec64Every8 {
  __m128i vec_ ;
  __m128i shiftLeftIndices_ ; // can be made static later
  static const int WORD_CNT_ = 8 ;
  static const int PERIOD_ = 8 ;

  void init_() {
    shiftLeftIndices_ = _mm_set_epi8(7, 6, 5, 4, 3, 2, 1, 0, 13, 12, 11, 10, 9, 8, 0xFF, 0xFF) ;
  }


 public:
  EDVec64Every8() {}

  explicit EDVec64Every8(__m128i v):vec_(v) {init_() ;}
  explicit EDVec64Every8(int16_t val):vec_(_mm_set1_epi16(val)) {init_() ;}

  // The input 64-bit bit-vector corresponds to the query characters. The probes we're interested
  // in are located at 8-bit intervals. The constructor below stores these 8 probes in a 128-bit
  // distance vector based on the layout: w7 w5 w3 w1 w6 w4 w2 w0.
  explicit EDVec64Every8(const BitVec64& bv): vec_(_mm_set_epi64x(bv.bitV>>8, bv.bitV)) {init_() ;}

  inline void setAll(int16_t val) {
    vec_ = _mm_set1_epi16(val) ;    
  }

  static int16_t getMaxWordVal() { return numeric_limits<int16_t>::max() ;}

  inline int getWord(int index) const {
    switch(index) {
    case 0: return _mm_extract_epi16(vec_, 0) ;
    case 1: return  _mm_extract_epi16(vec_, 4) ;
    case 2: return  _mm_extract_epi16(vec_, 1) ;
    case 3: return  _mm_extract_epi16(vec_, 5) ;
    case 4: return  _mm_extract_epi16(vec_, 2) ;
    case 5: return  _mm_extract_epi16(vec_, 6) ;
    case 6: return  _mm_extract_epi16(vec_, 3) ;
    case 7: return  _mm_extract_epi16(vec_, 7) ;
    default: assert(false); return 0 ;
    }
  }

  bool allLessThanOrEqualTo(const EDVec64Every8& other) const {
    // each elt in pred will be set to 0xFFFF if the element in this vector 
    // is greater than the corresponding element in other
    __m128i pred = _mm_cmpgt_epi16(vec_, other.vec_) ; 
    int flags = _mm_movemask_epi8(pred) ; // creates a mask from MSB of each 8-bit elt in pred

    return flags == 0 ; // if there's at least one elt greater than "other", flags != 0
  }
  
  inline void setWords (int* v) {
    vec_ = _mm_set_epi16(v[7], v[5], v[3], v[1], v[6], v[4], v[2], v[0]) ;
  }

  inline void setFirstWord(int val) {
    vec_ = _mm_insert_epi16(vec_, val, 0) ;
  }

  inline void setAllWords (uint16_t val) {
    vec_ = _mm_set1_epi16(val) ;
  }

  inline void setWordsAsMask () {
    vec_ = _mm_set1_epi16(0x1) ;
  }

  static int getLastWordIndexFor (int queryLen) {
    return (queryLen-1) / PERIOD_ ; // logical word index - no interleaving at this level
  }

  static int getProbeOffsetFor (int queryLen) {
    return (queryLen-1) % PERIOD_ ;
  }

  void setWordsAsEndBonus(int queryLen, int endBonus) {
    int v[WORD_CNT_] = {0, 0, 0, 0, 0, 0, 0, 0} ;
    
    int lastWordIndex = getLastWordIndexFor(queryLen) ;    
    v[lastWordIndex] = endBonus ;

    setWords(v) ;
  }

  void setWordsAsBadScore(int queryLen, int thr, int infScore) {
    int v[WORD_CNT_] = {thr, thr, thr, thr, thr, thr, thr, thr} ;
    
    int lastWordIndex = getLastWordIndexFor(queryLen) ;    
    for (int i=lastWordIndex+1; i < WORD_CNT_; ++i) {
      v[i] = infScore ;
    }

    setWords(v) ;
  }

  static int getDistAt(int wordIndex, int queryLen) {
    return wordIndex * PERIOD_ + getProbeOffsetFor(queryLen) + 1 ;
  }

  void setWordsAsDist (int queryLen, int distWt) {
    int v[WORD_CNT_] ;
    int bitOffset = getProbeOffsetFor(queryLen) ;
    
    for (int wi=0; wi < WORD_CNT_; ++wi) {
      v[wi] = distWt * (wi * PERIOD_ + bitOffset + 1) ;
    }

    setWords(v) ;
  }

  void setMin(const EDVec64Every8& other) {
    vec_ = _mm_min_epi16(vec_, other.vec_) ;
  }

  void setMin(const EDVec64Every8& other, EDVec64Every8& bestIndices, const EDVec64Every8& otherIndices) {
    __m128i pred = _mm_cmplt_epi16(other.vec_, vec_) ; // pred is 0xFFFF if other < this
    vec_ = BLENDV(vec_, other.vec_, pred) ; // if pred is 0, choose this; o/w choose other
    bestIndices.vec_ = BLENDV(bestIndices.vec_, otherIndices.vec_, pred) ;
  }

  void setMax(const EDVec64Every8& other, EDVec64Every8& bestIndices, const EDVec64Every8& otherIndices) {
    __m128i pred = _mm_cmpgt_epi16(other.vec_, vec_) ; // pred is 0xFFFF if other > this
    vec_ = BLENDV(vec_, other.vec_, pred) ; // if pred is 0, choose this; o/w choose other
    bestIndices.vec_ = BLENDV(bestIndices.vec_, otherIndices.vec_, pred) ;
  }

  void setMax(const EDVec64Every8& other, EDVec64Every8& bestIndices, const EDVec64Every8& otherIndices, EDVec64Every8& bestAccumDist, const EDVec64Every8& otherAccumDist) {
    __m128i pred = _mm_cmpgt_epi16(other.vec_, vec_) ; // pred is 0xFFFF if other > this
    vec_ = BLENDV(vec_, other.vec_, pred) ; // if pred is 0, choose this; o/w choose other
    bestIndices.vec_ = BLENDV(bestIndices.vec_, otherIndices.vec_, pred) ;
    bestAccumDist.vec_ = BLENDV(bestAccumDist.vec_, otherAccumDist.vec_, pred) ;
  }


  // The return value will be nonzero iff at least one of the entries is updated.
  // 2 bits per word is returned because of SSE limitations.
  uint32_t setMaxAndReturnFlag(const EDVec64Every8& other, EDVec64Every8& bestIndices, const EDVec64Every8& otherIndices) {
    __m128i pred = _mm_cmpgt_epi16(other.vec_, vec_) ; // pred is 0xFFFF if other > this
    vec_ = BLENDV(vec_, other.vec_, pred) ; // if pred is 0, choose this; o/w choose other
    bestIndices.vec_ = BLENDV(bestIndices.vec_, otherIndices.vec_, pred) ;
    return (uint32_t) _mm_movemask_epi8(pred) ;
  }

  // The return value will be nonzero iff at least one of the entries is updated.
  // 2 bits per word is returned because of SSE limitations.
  uint32_t setMaxAndReturnFlag(const EDVec64Every8& other, EDVec64Every8& bestIndices, const EDVec64Every8& otherIndices, EDVec64Every8& bestAccumDist, const EDVec64Every8& otherAccumDist) {
    __m128i pred = _mm_cmpgt_epi16(other.vec_, vec_) ; // pred is 0xFFFF if other > this
    vec_ = BLENDV(vec_, other.vec_, pred) ; // if pred is 0, choose this; o/w choose other
    bestIndices.vec_ = BLENDV(bestIndices.vec_, otherIndices.vec_, pred) ;
    bestAccumDist.vec_ = BLENDV(bestAccumDist.vec_, otherAccumDist.vec_, pred) ;
 
   return (uint32_t) _mm_movemask_epi8(pred) ;
  }

  void addThirdIfFirstGTSecond(const EDVec64Every8& first, const EDVec64Every8& second, 
			       const EDVec64Every8& third, const EDVec64Every8& zero) {

    __m128i pred = _mm_cmpgt_epi16(first.vec_, second.vec_) ; 
    // pred is 0xFFFF if first > second

    // If pred is 0, then blendv will add zero; o/w it will add third
    vec_ = _mm_add_epi16(vec_, BLENDV(zero.vec_, third.vec_, pred)) ;
  }

  EDVec64Every8 subSat(const EDVec64Every8& other) const {
    return EDVec64Every8(_mm_subs_epu16(this->vec_, other.vec_)) ;
  }

  inline EDVec64Every8 shiftBitsRightWithinWords(int shiftVal) {
    return EDVec64Every8(_mm_srli_epi16(vec_, shiftVal)) ;
  }

  inline void shiftWordsLeftByOne() {

#ifdef USE_SSE4
    vec_ = _mm_shuffle_epi8(vec_, shiftLeftIndices_) ;
#else

    // The layout of the logical 16-bit words before shift:
    // 7 5 3 1 6 4 2 0
    // Consider the physical 16-bit words before shift: 
    // 7 6 5 4 3 2 1 0
    // These words should be shuffled into the following for logical shift:
    // 3 2 1 0 6 5 4 X
    
    // First, do the following: 7 6 5 4 3 2 1 0 --> 6 5 4 7 3 2 1 0
    // The binary imm value: 10 01 00 11 (i.e. 2 1 0 3)
    vec_ = _mm_shufflehi_epi16(vec_, 0x93) ;

    // Then, do the following: 6 5 4 7 3 2 1 0 --> 3 2 1 0 6 5 4 7
    // The binary imm value: 01 00 11 10 (i.e. 1 0 3 2)
    vec_ = _mm_shuffle_epi32(vec_, 0x4E) ;

#endif // #ifdef USE_SSE4

  }

  inline bool operator == (const EDVec64Every8& other) {
    return _mm_movemask_epi8(_mm_cmpeq_epi16(vec_, other.vec_)) == 0xFFFF ;
  }

  inline EDVec64Every8& operator += (const EDVec64Every8& other) {
    vec_ = _mm_add_epi16(vec_, other.vec_) ;
    return *this ;
  }

  inline EDVec64Every8& operator -= (const EDVec64Every8& other) {
    vec_ = _mm_sub_epi16(vec_, other.vec_) ;
    return *this ;
  }

  inline EDVec64Every8 operator+ (const EDVec64Every8& other) const {
    return EDVec64Every8(_mm_add_epi16(vec_, other.vec_)) ;
  }

  inline EDVec64Every8 operator- (const EDVec64Every8& other) const {
    return EDVec64Every8(_mm_sub_epi16(vec_, other.vec_)) ;
  }

  inline EDVec64Every8 operator* (const EDVec64Every8& other) const {
    return EDVec64Every8(_mm_mullo_epi16(vec_, other.vec_)) ;
  }

  inline EDVec64Every8 operator& (const EDVec64Every8& other) const {
    return EDVec64Every8(_mm_and_si128(vec_, other.vec_)) ;
  }

  static int WORD_CNT() {
    return WORD_CNT_ ;
  }

  static int PERIOD() {
    return PERIOD_ ;
  }

  friend std::ostream& operator<< (std::ostream& os, const EDVec64Every8& v) {
    os << "{" ;
    for (int i=0; i < WORD_CNT_; ++i) {
      os << v.getWord(i) << " " ;
    }
    os << "}" ;
    return os ;
  }

} ;




#endif // #ifndef _FAST_EXTEND_VEC64_H