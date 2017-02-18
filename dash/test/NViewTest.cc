
#include "NViewTest.h"

#include <gtest/gtest.h>

#include <dash/View.h>
#include <dash/Array.h>
#include <dash/Matrix.h>

#include <array>


namespace dash {
namespace test {

  template <class MatrixT>
  void initialize_matrix(MatrixT & matrix) {
    if (dash::myid() == 0) {
      for(size_t i = 0; i < matrix.extent(0); ++i) {
        for(size_t k = 0; k < matrix.extent(1); ++k) {
          matrix[i][k] = (i + 1) * 0.100 + (k + 1) * 0.001;
        }
      }
    }
    matrix.barrier();

    for(size_t i = 0; i < matrix.local_size(); ++i) {
      matrix.lbegin()[i] += dash::myid();
    }
    matrix.barrier();
  }

  template <class ValueRange>
  std::string range_str(
    const ValueRange & vrange) {
    typedef typename ValueRange::value_type value_t;
    std::ostringstream ss;
    const auto & idx = dash::index(vrange);
    int          i   = 0;
    for (const auto & v : vrange) {
      ss << "[" << *(dash::begin(idx) + i) << "] "
         << static_cast<value_t>(v) << " ";
      ++i;
    }
    return ss.str();
  }

  template <class NViewType>
  void print_nview(
    const std::string & name,
    const NViewType   & nview) {
    auto view_nrows = nview.extents()[0];
    auto view_ncols = nview.extents()[1];
    for (int r = 0; r < view_nrows; ++r) {
      std::vector<double> row_values;
      for (int c = 0; c < view_ncols; ++c) {
        row_values.push_back(
          static_cast<double>(nview[r * view_ncols + c]));
      }
      DASH_LOG_DEBUG("NViewTest.print_nview",
                     name, "[", r, "]", row_values);
    }
  }
}
}

using dash::test::range_str;

TEST_F(NViewTest, ViewTraits)
{
  dash::Matrix<int, 2> matrix(dash::size() * 10);
  auto v_sub  = dash::sub(0, 10, matrix);
  auto i_sub  = dash::index(v_sub);
  auto v_ssub = dash::sub(0, 5, (dash::sub(0, 10, matrix)));
  auto v_loc  = dash::local(matrix);

  static_assert(
      dash::view_traits<decltype(v_sub)>::is_view::value == true,
      "view traits is_view for sub(dash::Matrix) not matched");
  static_assert(
      dash::view_traits<decltype(v_ssub)>::is_view::value == true,
      "view traits is_view for sub(sub(dash::Matrix)) not matched");
  static_assert(
      dash::view_traits<decltype(v_sub)>::is_origin::value == false,
      "view traits is_origin for sub(dash::Matrix) not matched");
  static_assert(
      dash::view_traits<decltype(v_ssub)>::is_origin::value == false,
      "view traits is_origin for sub(sub(dash::Matrix)) not matched");
}

TEST_F(NViewTest, MatrixBlocked1DimSingleViewModifiers)
{
  auto nunits = dash::size();

  int block_rows = 3;
  int block_cols = 4;

  int nrows = nunits * block_rows;
  int ncols = nunits * block_cols;

  // columns distributed in blocks of same size:
  //
  //  0 0 0 | 1 1 1 | 2 2 2 | ...
  //  0 0 0 | 1 1 1 | 2 2 2 | ...
  //  0 0 0 | 1 1 1 | 2 2 2 | ...
  //
  dash::Matrix<double, 2> mat(
      dash::SizeSpec<2>(
        nrows,
        ncols),
      dash::DistributionSpec<2>(
        dash::NONE,
        dash::TILE(block_cols)),
      dash::Team::All(),
      dash::TeamSpec<2>(
        1,
        nunits));

  dash::test::initialize_matrix(mat);

  DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimLocalView",
                 "Matrix initialized");

  if (dash::myid() == 0) {
    dash::test::print_nview("matrix", dash::sub<0>(0, mat.extent(0), mat));
  }
  mat.barrier();

  // select first 2 matrix rows:
  auto nview_total  = dash::sub<0>(0, mat.extent(0), mat);
  auto nview_local  = dash::local(nview_total);
  auto nview_rows_g = dash::sub<0>(1, 3, mat);
  auto nview_cols_g = dash::sub<1>(2, 7, mat);
  auto nview_cr_s_g = dash::sub<1>(2, 7, nview_rows_g);
  auto nview_rc_s_g = dash::sub<0>(1, 3, nview_cols_g);

  if (dash::myid() == 0) {
    DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimLocalView",
                   "mat ->",
                   "offsets:", mat.offsets(),
                   "extents:", mat.extents(),
                   "size:",    mat.size());

    DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimLocalView",
                   "sub<0>(1,3, mat) ->",
                   "offsets:", nview_rows_g.offsets(),
                   "extents:", nview_rows_g.extents(),
                   "size:",    nview_rows_g.size());
    dash::test::print_nview("index_rows_g", dash::index(nview_rows_g));
    dash::test::print_nview("nview_rows_g", nview_rows_g);

    EXPECT_EQ_U(2,             nview_rows_g.extent<0>());
    EXPECT_EQ_U(mat.extent(1), nview_rows_g.extent<1>());

    DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimLocalView",
                   "sub<1>(2,7, mat) ->",
                   "offsets:", nview_cols_g.offsets(),
                   "extents:", nview_cols_g.extents(),
                   "size:",    nview_cols_g.size());
    dash::test::print_nview("index_cols_g", dash::index(nview_cols_g));
    dash::test::print_nview("nview_cols_g", nview_cols_g);

    EXPECT_EQ_U(mat.extent(0), nview_cols_g.extent<0>());
    EXPECT_EQ_U(5,             nview_cols_g.extent<1>());
 
#if 0
    DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimLocalView",
                   "sub<1>(2,7, sub<0>(1,3, mat) ->",
                   "offsets:", nview_cr_s_g.offsets(),
                   "extents:", nview_cr_s_g.extents(),
                   "size:",    nview_cr_s_g.size());
    dash::test::print_nview("index_cr_s_g", dash::index(nview_cr_s_g));
    dash::test::print_nview("nview_cr_s_g", nview_cr_s_g);
 
    DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimLocalView",
                   "sub<0>(1,3, sub<0>(2,7, mat) ->",
                   "offsets:", nview_rc_s_g.offsets(),
                   "extents:", nview_rc_s_g.extents(),
                   "size:",    nview_rc_s_g.size());
    dash::test::print_nview("index_rc_s_g", dash::index(nview_rc_s_g));
    dash::test::print_nview("nview_rc_s_g", nview_rc_s_g);
#endif
  }

  DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimLocalView",
                 "local(sub<0>(1,3, mat)) ->",
                 "offsets:", nview_local.offsets(),
                 "extents:", nview_local.extents(),
                 "size:",    nview_local.size());
  dash::test::print_nview("index_local", dash::index(nview_local));
  dash::test::print_nview("nview_local", nview_local);

  EXPECT_EQ_U(mat.extent(0), nview_local.extent<0>());
  EXPECT_EQ_U(block_cols,    nview_local.extent<1>());

  return;

  auto nview_rows_l = dash::local(nview_rows_g);

  EXPECT_EQ_U(2,             nview_rows_g.extent<0>());
  EXPECT_EQ_U(mat.extent(1), nview_rows_g.extent<1>());

  EXPECT_EQ_U(nview_rc_s_g.extents(), nview_cr_s_g.extents());
  EXPECT_EQ_U(nview_rc_s_g.offsets(), nview_cr_s_g.offsets());

  EXPECT_EQ_U(2,             nview_rows_l.extent<0>());
  EXPECT_EQ_U(block_cols,    nview_rows_l.extent<1>());
}

TEST_F(NViewTest, MatrixBlocked1DimSub)
{
  auto nunits = dash::size();

  int block_rows = 4;
  int block_cols = 3;

  int nrows = nunits * block_rows;
  int ncols = nunits * block_cols;

  // columns distributed in blocks of same size:
  //
  //  0 0 0 | 1 1 1 | 2 2 2 | ...
  //  0 0 0 | 1 1 1 | 2 2 2 | ...
  //  0 0 0 | 1 1 1 | 2 2 2 | ...
  //
  dash::Matrix<double, 2> mat(
      dash::SizeSpec<2>(
        nrows,
        ncols),
      dash::DistributionSpec<2>(
        dash::NONE,
        dash::TILE(block_cols)),
      dash::Team::All(),
      dash::TeamSpec<2>(
        1,
        nunits));

  dash::test::initialize_matrix(mat);

  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", mat.extents());
  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub",
                     mat.pattern().local_extents());
  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub",
                     mat.pattern().local_size());

  if (dash::myid() == 0) {
    auto all_sub = dash::sub<0>(
                         0, mat.extents()[0],
                         mat);

    DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimSub",
                   dash::internal::typestr(all_sub));

    DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", all_sub.extents());
    DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", all_sub.extent(0));
    DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", all_sub.extent(1));
    DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", all_sub.size(0));
    DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", all_sub.size(1));
    DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub",
                       index(all_sub).size());

    dash::test::print_nview("all_index", dash::index(all_sub));
    dash::test::print_nview("all_view",  all_sub);
  }

  mat.barrier();

  // -- Sub-Section ----------------------------------
  //
  
  if (dash::myid() == 0) {
    auto nview_sub  = dash::sub<0>(1, nrows - 1,
                        dash::sub<1>(1, ncols - 1,
                          mat));
    auto nview_rows = nview_sub.extent<0>();
    auto nview_cols = nview_sub.extent<1>();

    DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", nview_sub.extents());
    DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", nview_sub.extent(0));
    DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", nview_sub.extent(1));
    DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", nview_sub.size(0));
    DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", nview_sub.size(1));
    DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub",
                       index(nview_sub).size());

    dash::test::print_nview("nview_sub", nview_sub);
    dash::test::print_nview("index_sub", dash::index(nview_sub));

//  EXPECT_EQ_U(nview_rows, nview_sub.extent(0));
//  EXPECT_EQ_U(nview_rows, mat.extent(0) - 2);
//  EXPECT_EQ_U(nview_cols, nview_sub.extent(1));
//  EXPECT_EQ_U(nview_cols, mat.extent(1) - 2);
//  
//  for (int r = 0; r < nview_rows; ++r) {
//    auto row_view = dash::sub<0>(r, r+1, nview_sub);
//    DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimSub",
//                   "row[", r, "]",
//                   range_str(row_view));
//  }
//  for (int r = 0; r < nview_rows; ++r) {
//    auto row_view  = dash::sub<0>(r, r+1, nview_sub);
//    auto row_index = dash::index(row_view);
//    DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimSub",
//                   "index row[", r, "]",
//                   range_str(row_index));
//  }
  }

  // -- Local View -----------------------------------
  //
  
  auto loc_view = dash::local(
                    dash::sub<0>(
                      0, mat.extents()[0],
                      mat));

  EXPECT_EQ_U(2, decltype(loc_view)::rank::value);
  EXPECT_EQ_U(2, loc_view.ndim());
  
  int  lrows    = loc_view.extent<0>();
  int  lcols    = loc_view.extent<1>();

  DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimSub",
                 dash::internal::typestr(loc_view));
  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", loc_view.extents());
  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", loc_view.extent(0));
  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", loc_view.extent(1));
  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", loc_view.size(0));
  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", loc_view.size(1));
  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub", loc_view.size());
  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub",
                     index(loc_view).size());
 
  return;
 
  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub",
                     loc_view.begin().pos());
  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub",
                     loc_view.end().pos());
  DASH_LOG_DEBUG_VAR("NViewTest.MatrixBlocked1DimSub",
                     (loc_view.end() - loc_view.begin()));
  DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimSub",
                 "loc_view:", range_str(loc_view));
 
  EXPECT_EQ_U(mat.local_size(), lrows * lcols);

  for (int r = 0; r < lrows; ++r) {
    std::vector<double> row_values;
    for (int c = 0; c < lcols; ++c) {
      row_values.push_back(
        static_cast<double>(*(loc_view.begin() + (r * lrows + c))));
    }
    DASH_LOG_DEBUG("NViewTest.MatrixBlocked1DimSub",
                   "lrow[", r, "]", row_values);
  }
}

