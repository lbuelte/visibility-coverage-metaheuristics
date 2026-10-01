#ifndef GISCUPBONN_KMEANSCLUSTERING
#define GISCUPBONN_KMEANSCLUSTERING

#include <utility>
#include <vector>
class KMeansClustering {
public:
    KMeansClustering(const std::vector<std::pair<double, double>> &points, unsigned k, unsigned seed);

    void run();

    const std::vector<unsigned> &clusters_by_ids() const;

    unsigned cluster_by_ids(unsigned id) const;

    const std::vector<std::pair<double, double>> &cluster_centers() const;
    
private:
    bool has_run = false;
    
    const std::vector<std::pair<double, double>> &points;

    unsigned k, num_points;

    unsigned seed;

    std::vector<unsigned> cluster_of;

    std::vector<std::pair<double, double>> cluster_center;

    std::vector<unsigned> points_per_cluster;
};

#endif // GISCUPBONN_KMEANSCLUSTERING
