/**
 * \example ex.11.halo-stencil/main.cpp
 *
 * Stencil codes are iterative kernels on arrays of at least 2 dimensions
 * where the value of an array element at iteration i+1 depends on the values
 * of its neighbors in iteration i.
 *
 * Calculations of this kind are very common in scientific applications, e.g.
 * in iterative solvers and filters in image processing.
 *
 * This example implements a very simple blur filter. For simplicity
 * no real image is used, but an image containg circles is generated.
 * 
 */

#include <dash/Init.h>
#include <dash/Matrix.h>
#include <dash/Dimensional.h>
#include <dash/TeamSpec.h>

#include <dash/algorithm/Copy.h>
#include <dash/algorithm/Fill.h>

#include <dash/experimental/HaloMatrix.h>

#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <thread>


using namespace std;
using namespace dash::experimental;

using element_t = unsigned char;
using Pattern_t = dash::Pattern<2>;
using index_t   = typename Pattern_t::index_type;
using Array_t   = dash::NArray<element_t, 2, index_t, Pattern_t>;
using Halo_t    = HaloSpec<2>;
using HArray_t  = HaloMatrix<Array_t, Halo_t>;

void write_pgm(const std::string & filename, const Array_t & data){
  if(dash::myid() == 0){

    auto ext_x = data.extent(0);
    auto ext_y = data.extent(1);
    std::ofstream file;
    file.open(filename);

    file << "P2\n" << ext_x << " " << ext_y << "\n"
         << "255" << std::endl;

    // Buffer of matrix rows
    std::vector<element_t> buffer(ext_x);

    for(long y=0; y<ext_y; ++y){
      const auto & first = data.begin();

      dash::copy(first+ext_x*y, first+ext_x*(y+1), buffer.data());

      for(long x=0; x<ext_x; ++x){
        file << setfill(' ') << setw(3)
             << static_cast<int>(buffer[x]) << " ";
      }
      file << std::endl;
    }
    file.close();
  }
  dash::barrier();
}

void set_pixel(Array_t & data, index_t x, index_t y){
  const element_t color = 1;
  auto ext_x = data.extent(0);
  auto ext_y = data.extent(1);

  x = (x+ext_x)%ext_x;
  y = (y+ext_y)%ext_y;

  data.at(x, y) = color;
}

void draw_circle(Array_t * dataptr, index_t x0, index_t y0, int r){
  // Check who owns center, owner draws
  auto & data = *dataptr;
  if(!data.at(x0, y0).is_local()){
    return;
  }

  int       f     = 1-r;
  int       ddF_x = 1;
  int       ddF_y = -2*r;
  index_t   x     = 0;
  index_t   y     = r;

  set_pixel(data, x0 - r, y0);
  set_pixel(data, x0 + r, y0);
  set_pixel(data, x0, y0 - r);
  set_pixel(data, x0, y0 + r);

  while(x<y){
    if(f>=0){
      y--;
      ddF_y+=2;
      f+=ddF_y;
    }
    ++x;
    ddF_x+=2;
    f+=ddF_x;
    set_pixel(data, x0+x, y0+y);
    set_pixel(data, x0-x, y0+y);
    set_pixel(data, x0+x, y0-y);
    set_pixel(data, x0-x, y0-y);
    set_pixel(data, x0+y, y0+x);
    set_pixel(data, x0-y, y0+x);
    set_pixel(data, x0+y, y0-x);
    set_pixel(data, x0-y, y0-x);
  }
}

void smooth(Array_t & data_old, Array_t & data_new){
  // Add Halo Features
  Halo_t halo_s({{{-1,1},{-1,1}}});
  HArray_t hdata_old(data_old, halo_s);
  HArray_t hdata_new(data_new, halo_s);

  const auto & pattern = data_old.pattern();

  auto lext_x = pattern.local_extent(0);
  auto lext_y = pattern.local_extent(1);

  auto olptr = data_old.lbegin();
  auto nlptr = data_new.lbegin();

  // Fetch Halo
  hdata_old.updateHalosAsync();

  // Inner cell
  for( index_t x=1; x<lext_x-1; x++ ) {
    for( index_t y=1; y<lext_y-1; y++ ) {
      nlptr[x*lext_y+y] =
        ( 0.40 * olptr[x*lext_y+y] +
        0.15 * olptr[(x-1)*lext_y+y] +
        0.15 * olptr[(x+1)*lext_y+y] +
        0.15 * olptr[x*lext_y+y-1] +
        0.15 * olptr[x*lext_y+y+1]);
    }
  }
  // Boundary

  // Wait until all Halo updates ready
  hdata_old.waitHalosAsync();

  // Calculation of boundary Halo elements
  for(auto it = hdata_old.bbegin(); it != hdata_old.bend(); ++it) 
  {
    auto core = *it;
    *(nlptr+it.lpos()) = (0.40 * core) +
                       (0.15 * it.halo_value(-1,  0)) +
                       (0.15 * it.halo_value( 1,  0)) +
                       (0.15 * it.halo_value( 0, -1)) +
                       (0.15 * it.halo_value( 0, +1));
  }
  data_new.barrier();
}

int main(int argc, char* argv[])
{
  int sizex = 1000;
  int sizey = 1000;
  int niter = 20;

  dash::init(&argc, &argv);
  
  // Prepare grid
  dash::TeamSpec<2> ts;
  dash::SizeSpec<2> ss(sizex, sizey);
  dash::DistributionSpec<2> ds(dash::BLOCKED, dash::BLOCKED);
  ts.balance_extents();

  Pattern_t pattern(ss, ds, ts);

  Array_t data_old(pattern);
  Array_t data_new(pattern);

  dash::fill(data_old.begin(), data_old.end(), 255);
  dash::fill(data_new.begin(), data_new.end(), 255);

  std::vector<std::thread> threads;
  threads.push_back(std::thread(draw_circle, &data_old, 0, 0, 40));
  threads.push_back(std::thread(draw_circle, &data_old, 0, 0, 30));
  threads.push_back(std::thread(draw_circle, &data_old, 100, 100, 10));
  threads.push_back(std::thread(draw_circle, &data_old, 100, 100, 20));
  threads.push_back(std::thread(draw_circle, &data_old, 100, 100, 30));
  threads.push_back(std::thread(draw_circle, &data_old, 100, 100, 40));
  threads.push_back(std::thread(draw_circle, &data_old, 100, 100, 50));
  threads.push_back(std::thread(draw_circle, &data_old, 500, 500, 400));

  for(auto & t : threads){
    t.join();
  }

  dash::barrier();
  write_pgm("testimg_input.pgm", data_old);
  dash::barrier();

  for(int i=0; i<niter; ++i){
    // switch references
    auto & data_prev = i%2 ? data_new : data_old;
    auto & data_next = i%2 ? data_old : data_new;

    smooth(data_prev, data_next);
    dash::barrier();
  }

  // Assume niter is even
  write_pgm("testimg_output.pgm", data_new);
  dash::finalize();
}
