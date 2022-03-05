#pragma once
#include <memory>
#include <string>
#include <unordered_map>

class LockSet {
public:
  std::string toString() const;
  std::shared_ptr<LockSet> clone() const;
  void addLock(uint64_t lock);
  void removeLock(uint64_t lock);
  LockSet() = default;
  ~LockSet() = default;
  LockSet(const LockSet& lockset);
  friend bool hasCommonLockImpl(const LockSet& l1, const LockSet& l2);
  friend bool isSubSetImpl(const LockSet& l1, const LockSet& l2);
  bool isEmpty() const;
private:
  std::unordered_map<uint64_t, uint64_t> mLock; 
};

bool hasCommonLock(LockSet* l1, LockSet* l2);
bool isSubSet(LockSet* l1, LockSet* l2);
