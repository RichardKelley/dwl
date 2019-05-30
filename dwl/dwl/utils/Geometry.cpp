#include <dwl/utils/Math.h>


namespace dwl
{

namespace math
{

void normalizeAngle(double& angle,
					AngleRepresentation angle_notation)
{
	switch (angle_notation) {
		case ZeroTo2Pi:
			// Normalizing the angle between [0,2*pi]
			if ((angle >= (2 * M_PI)) || (angle <= 0))
				angle -= floor(angle / (2 * M_PI)) * (2 * M_PI);
			break;

		case MinusPiToPi:
			// Normalizing the angle between [0,2*pi]
			if ((angle >= (2 * M_PI)) || (angle <= 0))
				angle -= floor(angle / (2 * M_PI)) * (2 * M_PI);

			// Normalizing the angle between [0,pi]
			if ((angle > M_PI) && (angle < (2 * M_PI))) {
				angle -= 2 * M_PI;
			}
			break;

		default:
			printf(YELLOW_ "Warning: it was not normalize the angle"
					" because the angle notation is incoherent \n" COLOR_RESET);
			break;
	}
}


void inRadiiTriangle(double& inradii,
					 double size_a,
					 double size_b,
					 double size_c)
{
	double s = (size_a + size_b + size_c) / 2;
	double a = (s - size_a) * (s - size_b) * (s - size_c) / s;

	if (a > 0)
		inradii = sqrt(a);
	else
		inradii = 0;
}


void computePlaneParameters(Eigen::Vector3d& normal,
							const std::vector<Eigen::Vector3f>& points)
{
	unsigned int num_points = points.size();
	if (num_points < 3) {
		printf(YELLOW_ "Warning: could not computed the plane parameter"
				" with less of 3 points\n" COLOR_RESET);
	} else if (num_points == 3) {
		// Determinate the sign of the angles in order to compute a normal
		// that follows the right-handed rule
		Eigen::Vector3d a = (points[1] - points[0]).cast<double>();
		Eigen::Vector3d b = (points[2] - points[0]).cast<double>();
		Eigen::Vector2d p((double) -b(1), (double) b(0));
		double b_coord = a.head<2>().dot(b.head<2>());
		double p_coord = a.head<2>().dot(p);

		// Computing the normal as a x b or b x a
		if (atan2f(p_coord, b_coord) > 0)
			normal = b.cross(a);
		else
			normal = a.cross(b);
		normal /= normal.norm();
	} else {
		Eigen::Matrix3d covariance;
		Eigen::Vector3d mean;
		double curvature;

		computeMeanAndCovarianceMatrix(mean, covariance, points);
		solvePlaneParameters(normal, curvature, covariance);
	}
}


unsigned int computeMeanAndCovarianceMatrix(Eigen::Vector3d& mean,
											Eigen::Matrix3d& covariance_matrix,
											const std::vector<Eigen::Vector3f>& cloud)
{
	// create the buffer on the stack which is much faster than using
	// cloud[indices[i]] and mean as a buffer
	Eigen::VectorXd accu = Eigen::VectorXd::Zero(9, 1);

	for (size_t i = 0; i < cloud.size(); ++i) {
		accu[0] += cloud[i](0) * cloud[i](0);
		accu[1] += cloud[i](0) * cloud[i](1);
		accu[2] += cloud[i](0) * cloud[i](2);
		accu[3] += cloud[i](1) * cloud[i](1);
		accu[4] += cloud[i](1) * cloud[i](2);
		accu[5] += cloud[i](2) * cloud[i](2);
		accu[6] += cloud[i](0);
		accu[7] += cloud[i](1);
		accu[8] += cloud[i](2);
	}

	accu /= (cloud.size());
	if (cloud.size() != 0) {
		mean[0] = accu[6];
		mean[1] = accu[7];
		mean[2] = accu[8];

		covariance_matrix.coeffRef(0) = accu[0] - accu[6] * accu[6];
		covariance_matrix.coeffRef(1) = accu[1] - accu[6] * accu[7];
		covariance_matrix.coeffRef(2) = accu[2] - accu[6] * accu[8];
		covariance_matrix.coeffRef(4) = accu[3] - accu[7] * accu[7];
		covariance_matrix.coeffRef(5) = accu[4] - accu[7] * accu[8];
		covariance_matrix.coeffRef(8) = accu[5] - accu[8] * accu[8];
		covariance_matrix.coeffRef(3) = covariance_matrix.coeff(1);
		covariance_matrix.coeffRef(6) = covariance_matrix.coeff(2);
		covariance_matrix.coeffRef(7) = covariance_matrix.coeff(5);
	}

	return (static_cast<unsigned int> (cloud.size()));
}


void solvePlaneParameters(Eigen::Vector3d &normal_vector,
						  double &curvature,
						  const Eigen::Matrix3d covariance_matrix)
{
	// Extract the smallest eigenvalue and its eigenvector
	EIGEN_ALIGN16 Eigen::Vector3d::Scalar eigenvalue;
	EIGEN_ALIGN16 Eigen::Vector3d eigenvector;

	//typedef typename Eigen::Matrix3f::Scalar Scalar;
	// Scale the matrix so its entries are in [-1,1]. The scaling is applied
	// only when at least one matrix entry has magnitude larger than 1.
	Eigen::Matrix3d::Scalar scale = covariance_matrix.cwiseAbs().maxCoeff();
	if (scale <= std::numeric_limits<Eigen::Matrix3d::Scalar>::min())
		scale = Eigen::Matrix3d::Scalar(1.0);

	Eigen::Matrix3d scaledMat = covariance_matrix / scale;

	Eigen::Vector3d eigenvalues;
	computeRoots(eigenvalues, scaledMat);

	eigenvalue = eigenvalues(0) * scale;

	scaledMat.diagonal().array() -= eigenvalues(0);

	Eigen::Vector3d vec1 = scaledMat.row(0).cross(scaledMat.row(1));
	Eigen::Vector3d vec2 = scaledMat.row(0).cross(scaledMat.row(2));
	Eigen::Vector3d vec3 = scaledMat.row(1).cross(scaledMat.row(2));

	Eigen::Matrix3d::Scalar len1 = vec1.squaredNorm();
	Eigen::Matrix3d::Scalar len2 = vec2.squaredNorm();
	Eigen::Matrix3d::Scalar len3 = vec3.squaredNorm();

	if (len1 >= len2 && len1 >= len3)
		eigenvector = vec1 / std::sqrt(len1);
	else if (len2 >= len1 && len2 >= len3)
		eigenvector = vec2 / std::sqrt(len2);
	else
		eigenvector = vec3 / std::sqrt(len3);

	// The normal vectors should point to the Z direction
	if (eigenvector(2) < 0.)
		eigenvector *= -1;

	normal_vector = eigenvector;

	// Compute the curvature surface change
	float eig_sum = covariance_matrix.coeff(0) +
			covariance_matrix.coeff(4) + covariance_matrix.coeff(8);
	if (eig_sum != 0)
		curvature = fabsf(eigenvalue / eig_sum);
	else
		curvature = 0;
}


void computeRoots(Eigen::Vector3d& roots,
				  const Eigen::Matrix3d& m)
{
	// The characteristic equation is x^3 - c2*x^2 + c1*x - c0 = 0. The
	// eigenvalues are the roots to this equation, all guaranteed to be
	// real-valued, because the matrix is symmetric.
	Eigen::Matrix3d::Scalar c0 = m(0,0) * m(1,1) * m(2,2) +
			Eigen::Matrix3d::Scalar(2) * m(0,1) * m(0,2) * m(1,2)
	- m(0,0) * m(1,2) * m(1,2) - m(1,1) * m(0,2) * m(0,2)
	- m(2,2) * m(0,1) * m(0,1);

	Eigen::Matrix3d::Scalar c1 = m(0,0) * m(1,1) - m(0,1) * m(0,1) + m(0,0) * m(2,2) -
			m(0,2) * m(0,2) + m(1,1) * m(2,2) - m(1,2) * m(1,2);

	Eigen::Matrix3d::Scalar c2 = m(0,0) + m(1,1) + m(2,2);


	if (fabs (c0) < Eigen::NumTraits<Eigen::Matrix3d::Scalar>::epsilon ())// one root is 0 -> quadratic equation
		computeRoots2(roots, c2, c1);
	else
	{
		const Eigen::Matrix3d::Scalar s_inv3 = Eigen::Matrix3d::Scalar(1.0 / 3.0);
		const Eigen::Matrix3d::Scalar s_sqrt3 = std::sqrt(Eigen::Matrix3d::Scalar (3.0));
		// Construct the parameters used in classifying the roots of the equation
		// and in solving the equation for the roots in closed form.
		Eigen::Matrix3d::Scalar c2_over_3 = c2*s_inv3;
		Eigen::Matrix3d::Scalar a_over_3 = (c1 - c2 * c2_over_3) * s_inv3;
		if (a_over_3 > Eigen::Matrix3d::Scalar(0))
			a_over_3 = Eigen::Matrix3d::Scalar (0);

		Eigen::Matrix3d::Scalar half_b = Eigen::Matrix3d::Scalar(0.5) *
				(c0 + c2_over_3 * (Eigen::Matrix3d::Scalar(2) * c2_over_3 * c2_over_3 - c1));

		Eigen::Matrix3d::Scalar q = half_b * half_b + a_over_3 * a_over_3 * a_over_3;
		if (q > Eigen::Matrix3d::Scalar(0))
			q = Eigen::Matrix3d::Scalar(0);

		// Compute the eigenvalues by solving for the roots of the polynomial.
		Eigen::Matrix3d::Scalar rho = sqrt(-a_over_3);
		Eigen::Matrix3d::Scalar theta = atan2(std::sqrt(-q), half_b) * s_inv3;
		Eigen::Matrix3d::Scalar cos_theta = cos(theta);
		Eigen::Matrix3d::Scalar sin_theta = sin(theta);
		roots(0) = c2_over_3 + Eigen::Matrix3d::Scalar(2) * rho * cos_theta;
		roots(1) = c2_over_3 - rho * (cos_theta + s_sqrt3 * sin_theta);
		roots(2) = c2_over_3 - rho * (cos_theta - s_sqrt3 * sin_theta);

		// Sort in increasing order.
		double roots0 = roots(0);
		double roots1 = roots(1);
		double roots2 = roots(2);
		if (roots(0) >= roots(1))
			std::swap(roots0, roots1);
		if (roots(1) >= roots(2)) {
			std::swap(roots1, roots2);
			if (roots(0) >= roots(1))
				std::swap(roots0, roots1);
		}
		roots(0) = roots0;
		roots(1) = roots1;
		roots(2) = roots2;

		if (roots(0) <= 0) // eigenvalue for symmetric positive semi-definite matrix can not be negative! Set it to 0
			computeRoots2(roots, c2, c1);
	}
}


void computeRoots2(Eigen::Vector3d& roots,
				   const Eigen::Matrix3d::Scalar& b,
				   const Eigen::Matrix3d::Scalar& c)
{
	roots(0) = Eigen::Matrix3d::Scalar(0);
	Eigen::Matrix3d::Scalar d = Eigen::Matrix3d::Scalar(b * b - 4.0 * c);
	if (d < 0.0) // no real roots!!!! THIS SHOULD NOT HAPPEN!
		d = 0.0;

	Eigen::Matrix3d::Scalar sd = sqrt(d);

	roots(2) = 0.5f * (b + sd);
	roots(1) = 0.5f * (b - sd);
}


double isRight(const Eigen::Vector3d p0,
			   const Eigen::Vector3d p1,
			   const Eigen::Vector3d p2)
{
	return (p2(X) - p0(X)) * (p1(Y) - p0(Y)) -
			(p1(X) - p0(X)) * (p2(Y) - p0(Y));
}


void clockwiseSort(std::vector<Eigen::Vector3d>& p)
{
	// Sorting clockwise
	for (unsigned int i = 1; i < p.size() - 1; i++) {
		for (unsigned int j = i + 1; j < p.size(); j++) {
	        // The point p2 should always be on the right to be cwise
			// thus if it is on the left <0 i swap
			if (isRight(p[0], p[i], p[j])  < 0.0) {
				Eigen::Vector3d tmp = p[i];
				p[i] = p[j];
				p[j] = tmp;
			}
		}
	}
}


void counterClockwiseSort(std::vector<Eigen::Vector3d>& p)
{
	// Sorting counter clockwise
	for (unsigned int i = 1; i < p.size() - 1; i++) {
		for (unsigned int j = i + 1; j < p.size(); j++) {
	        // The point p2 should always be on the left of the line
			// to be ccwise thus if it is on the right  >0  swap
			if (isRight(p[0], p[i], p[j])  > 0.0) {
				Eigen::Vector3d tmp = p[i];
				p[i] = p[j];
				p[j] = tmp;
			}
		}
	}
}


LineCoeff2d lineCoeff(const Eigen::Vector3d& pt0,
					  const Eigen::Vector3d& pt1,
					  bool normalize)
{
	// (p,q).dot(x-x0, y-y0)=0 implicit line equation
	// px + qy -rx0 -qy0 = 0 => px + qy + r = 0
	// To find (p,q) I know this is the normal to the line, so if
	// I have another point x1 a solution with (p,q) perpendicular to
	// the segment S=(x1- x0, y1-y0) is p= -(y1-y0) q=x1-x0 for which
	// (p,q).dot(S) = 0
	LineCoeff2d ret;
	ret.p = pt0(Y) - pt1(Y);
	ret.q = pt1(X) - pt0(X);
	ret.r = -ret.p * pt0(X) - ret.q * pt0(Y);

	// normalize the equation in order to intuitively use stability margins
	if (normalize) {
		double norm = hypot(ret.p, ret.q);
		ret.p /= norm;
		ret.q /= norm;
		ret.r /= norm;
	}

	return ret;
}

} //@namespace math
} //@namespace dwl
