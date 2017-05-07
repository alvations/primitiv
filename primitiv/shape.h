#ifndef PRIMITIV_SHAPE_H_
#define PRIMITIV_SHAPE_H_

#include <initializer_list>
#include <string>
#include <vector>

namespace primitiv {

/**
 * Data structure to represent the shape of the node.
 *
 * Examples:
 *   Shape({})       == Shape({1, 1, 1, ...}, 1): scalar
 *   Shape({n})      == Shape({n, 1, 1, ...}, 1): row vector
 *   Shape({n, m})   == Shape({n, m, 1, ...}, 1): matrix
 *   Shape({...}, k): k-parallelized data (mini-batch)
 */
class Shape {
public:
  Shape() = delete;
  Shape(const Shape &) = default;
  Shape(Shape &&) = default;
  Shape & operator=(const Shape &) = default;
  Shape & operator=(Shape && ) = default;

  /**
   * Creates a new Shape object.
   * @param dim Integer list to represent the dimension.
   * @param k Batch size.
   */
  Shape(const std::initializer_list<unsigned> &dim, const unsigned k = 1);

  /**
   * Returns the size of the i-th dimension.
   * @param i Dimension number to check.
   * @return Size of the i-th dimension.
   */
  inline unsigned dim_size(const unsigned i) const {
    return i < dim_.size() ? dim_[i] : 1;
  }

  /**
   * Returns the batch size.
   * @return Batch size.
   */
  inline unsigned batch_size() const {
    return k_;
  }

  /**
   * Returns the number of actual data in the node.
   * This value is equal to batch_size() * dim_size(0) * dim_size(1) * ...
   * @return Number of actual data in the node.
   */
  inline unsigned size() const {
    unsigned s = k_;
    for (const unsigned d : dim_) s *= d;
    return s;
  }

  /**
   * Returns a string representation of the shape.
   * The format is: "[n,m,...]xk"
   * @return Encoded string.
   */
  std::string to_string() const;

  /**
   * Compare this and another Shape object are same.
   * @param rhs target Shape object to compare.
   * @return true if this and rhs are same, false otherwise.
   */
  inline bool operator==(const Shape &rhs) const {
    return dim_ == rhs.dim_ && k_ == rhs.k_;
  }

  /**
   * Compare this and another Shape object are not same.
   * @param rhs target Shape object to compare.
   * @return true if this and rhs are not same, false otherwise.
   */
  inline bool operator!=(const Shape &rhs) const {
    return !operator==(rhs);
  }

private:
  std::vector<unsigned> dim_;
  unsigned k_;
};

}  // namespace primitiv

#endif  // PRIMITIV_SHAPE_H_
