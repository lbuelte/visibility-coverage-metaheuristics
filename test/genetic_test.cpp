#include "gtest/gtest.h"
#include "GeneticAlgorithm.hpp"

TEST(GeneticAlgorithm,planeside_axisparallel){
    Hyperplane vertical(Point2(1,0),1);
    Hyperplane horizontal(Point2(0,1),1);
    Point2 ul(0,2);
    Point2 ur(2,2);
    Point2 ll(0,0);
    Point2 lr(2,0);
    EXPECT_EQ(plane_side(ul,vertical),1);
    EXPECT_EQ(plane_side(ul,horizontal),0);
    EXPECT_EQ(plane_side(ur,vertical),0);
    EXPECT_EQ(plane_side(ur,horizontal),0);
    EXPECT_EQ(plane_side(ll,vertical),1);
    EXPECT_EQ(plane_side(ll,horizontal),1);
    EXPECT_EQ(plane_side(lr,vertical),0);
    EXPECT_EQ(plane_side(lr,horizontal),1);
}

TEST(GeneticAlgorithm,planeside_cross){
    Hyperplane uphill(Point2(-1,1),0);
    Hyperplane downhill(Point2(1,1),0);
    Point2 north(0,1);
    Point2 east(1,0);
    Point2 south(0,-1);
    Point2 west(-1,0);
    EXPECT_EQ(plane_side(north,uphill),0);
    EXPECT_EQ(plane_side(north,downhill),0);
    EXPECT_EQ(plane_side(east,uphill),1);
    EXPECT_EQ(plane_side(east,downhill),0);
    EXPECT_EQ(plane_side(south,uphill),1);
    EXPECT_EQ(plane_side(south,downhill),1);
    EXPECT_EQ(plane_side(west,uphill),0);
    EXPECT_EQ(plane_side(west,downhill),1);
}

TEST(GeneticAlgorithm,line_through_points){
    Point2 a(2,3);
    Point2 b(1,2);
    Hyperplane uphill = hyperplane_through_points(a,b);
    EXPECT_DOUBLE_EQ(CGAL::to_double(uphill.normal_vector.y()/uphill.normal_vector.x()),-1);
    EXPECT_DOUBLE_EQ(CGAL::to_double(uphill.normal_vector.x()+2*uphill.normal_vector.y()),CGAL::to_double(uphill.offset));
}