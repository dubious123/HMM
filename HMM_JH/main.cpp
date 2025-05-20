#include <iostream>
#include <array>
#include <ranges>
#include <algorithm>
#include <random>
#include <format>
#include <sstream>
#include "common.h"

using t_state		= uint8;
using t_observation = uint8;

// number of state.
constexpr auto N = 5;
// number of observation.
constexpr auto M = 10;
// sequence length
constexpr auto T = 10;

// lambda = (A, B, pi)

// P1 : compute P(O | lambda) -> allows us to choose the best-match model

// p2 : given O, find optimal Q -> uncover hidden part of the model

// p3 : given O, find optimal lambda (A, B, pi) -> train the model

// p3 (find model) -> p2 (understand physical meaning of the model state) -> p1 (score each model)

template <typename t, std::size_t n>
std::string format_arr(std::array<t, n>& arr)
{
	std::ostringstream oss;
	oss << "{\n";
	for (size_t i = 0; i < n; ++i)
	{
		if constexpr (std::is_same_v<t, float>)
		{
			oss << std::format("{:.3f}", arr[i]);
		}
		else
		{
			oss << std::format("{}", arr[i]);
		}

		if (i + 1 < n)
		{
			oss << ", ";
		}
	}
	oss << "\n}\n";
	return oss.str();
}

template <typename t>
std::string format_matrix(t& elem)
{
	if constexpr (std::is_same_v<t, float32> or std::is_same_v<t, double64>)
	{
		return std::format(" {:.3f}", elem);
	}
	else
	{
		return std::format(" {}", elem);
	}
}

template <typename t, std::size_t n>
std::string format_matrix(std::array<t, n>& arr)
{
	std::ostringstream oss;
	oss << "{\n";
	for (t& elem : arr)
	{
		oss << format_matrix(elem);
	}
	oss << "\n}\n";
	return oss.str();
}

// lambda
template <std::size_t state_count, std::size_t observation_count, std::size_t T>
struct model
{
	// transition probability distribution
	std::array<std::array<double64, state_count>, state_count> A;
	// observation symbol probability distribution
	std::array<std::array<double64, observation_count>, state_count> B;
	// initial state distribution
	std::array<double64, state_count> pi;

	// observation sequence
	std::array<uint8, T> observations;

	// alpha[t][state] : t에서 상태가 state일 확률 * 해당 상태에서 관측 O_t가 나올 확률
	// 0 ~ t 까지 observation이 주어졌을 때 t에서 상태 s 에 있을 확률
	// t 에서 s 에 있을 확률 = t - 1에서 모든 s에서의 alpha * 전이확률 의 합 * observation 확률
	std::array<std::array<double64, state_count>, T> alpha;

	// t 에서 s에 있을 때 0(t+1), O(t+2), ... , O(T) 가 발생할 확률
	// 마지막은 1, (이후에 발생될것이 없음)
	std::array<std::array<double64, state_count>, T> beta;

	// probability of being at state s on time t
	std::array<std::array<double64, state_count>, T> gamma;

	// ξ
	// probability of moving from state s1 to s2 on time t
	// xi(curr, next) gamma(curr) * A[curr][next] * gamma(next) -> X
	// xi(t, curr, next) = alpha[t][curr] * A[curr][next] * B[next][O[t+1] * beta[t+1][next]
	std::array<std::array<std::array<double64, state_count>, state_count>, T> xi;

	// δ
	// t까지 관측이 진행되었을 때 다음 상태에 도달하는 가장 높은 경로의 확률
	// alpha : t 에서 s일 총 확률
	// delta : t 에서 s일 가장 적절한 경로의 확률
	std::array<std::array<double64, state_count>, T> delta;

	// ψ
	// t에서 다음 상태에 도달하기 직전 최고 확률 경로의 직전 상태
	// psi[t][j] = argmax_i (delta[t-1][i] * A[i][j])
	std::array<std::array<t_state, state_count>, T> psi;

	// 최적의 state array
	std::array<t_state, state_count> path;

	void init_A_B_pi()
	{
		std::random_device						 rd;
		std::mt19937							 gen(rd());
		std::uniform_real_distribution<double64> dist(0.0, 1.0);

		for (size_t i = 0; i < state_count; ++i)
		{
			// 임의의 값 생성
			for (size_t j = 0; j < state_count; ++j)
			{
				A[i][j] = dist(gen);
			}

			// 합 계산
			double64 sum = *std::ranges::fold_left_first(A[i], std::plus {});

			// 각 원소를 합으로 나누어 정규화 (합 = 1)
			for (auto& val : A[i])
			{
				val /= sum;
			}
		}

		for (size_t i = 0; i < state_count; ++i)
		{
			// 임의의 값 생성
			for (size_t j = 0; j < observation_count; ++j)
			{
				B[i][j] = dist(gen);
			}

			// 합 계산
			double64 sum = *std::ranges::fold_left_first(B[i], std::plus {});

			// 각 원소를 합으로 나누어 정규화 (합 = 1)
			for (auto& val : B[i])
			{
				val /= sum;
			}
		}

		auto sum = 0.f;
		for (auto& val : pi)
		{
			val	 = dist(gen);
			sum += val;
		}

		for (auto& val : pi)
		{
			val /= sum;
		}
	}

	void gen_random_observations()
	{
		std::random_device					 rd;
		std::mt19937						 gen(rd());
		std::uniform_int_distribution<uint8> dist(0, observation_count);

		for (auto i : std::views::iota(0uz, T))
		{
			observations[i] = dist(gen);
		}
	}

	// fill alpha
	void forward()
	{
		for (auto state : std::views::iota(0uz, state_count))
			alpha[0][state] = pi[state] * B[state][observations[0]];

		for (auto t : std::views::iota(1uz, T))
		{
			for (auto curr_state : std::views::iota(0uz, state_count))
			{
				auto sum = 0.f;
				for (auto prev_state : std::views::iota(0uz, state_count))
				{
					sum += alpha[t - 1][prev_state] * A[prev_state][curr_state];
				}

				alpha[t][curr_state] = sum * B[curr_state][observations[t]];
			}
		}
	}

	// fill beta
	void backward()
	{
		for (auto state : std::views::iota(0uz, state_count))
		{
			beta[T - 1][state] = 1.f;
		}

		for (auto t : std::views::iota(0uz, T - 1) | std::views::reverse)
		{
			for (auto curr_state : std::views::iota(0uz, state_count))
			{
				for (auto next_state : std::views::iota(0uz, state_count))
				{
					beta[t][curr_state] += beta[t + 1][next_state] * A[curr_state][next_state] * B[next_state][observations[t + 1]];
				}
			}
		}
	}

	// compute possibility of observation given this model
	auto likelihood()
	{
		// sum alpha
		return std::ranges::fold_left_first(alpha[T - 1], std::plus {}).value();
		// sum beta
		//  return std::ranges::fold_left_first(std::views::iota(0, N) | std::views::transform([&](auto state) { return pi[state] * B[state][O[0]] * beta[0][state]; }), std::plus {}).value();
	}

	void init_gamma()
	{
		auto p = likelihood();
		for (auto t : std::views::iota(0uz, T))
		{
			for (auto state : std::views::iota(0uz, state_count))
			{
				gamma[t][state] = alpha[t][state] * beta[t][state] / p;
			}
		}

		// sum of gamma[t] == 1;
	}

	void init_xi()
	{
		auto p = likelihood();
		for (auto t : std::views::iota(0uz, T - 1))
		{
			for (auto curr_state : std::views::iota(0uz, state_count))
			{
				for (auto next_state : std::views::iota(0uz, state_count))
				{
					xi[t][curr_state][next_state] = alpha[t][curr_state] * A[curr_state][next_state] * B[next_state][observations[t + 1]] * beta[t + 1][next_state] / p;
				}
			}
		}
	}

	// init delta, psi, and path
	void viterbi()
	{
		for (auto s : std::views::iota(0uz, state_count))
		{
			delta[0][s] = pi[s] * B[s][observations[0]];
			psi[0][s]	= 0;	// dummy
		}

		for (auto t : std::views::iota(1uz, T))
		{
			for (auto curr_state : std::views::iota(0uz, state_count))
			{
				auto max_prob  = 0.f;
				auto max_state = 0;
				for (auto prev_state : std::views::iota(0uz, state_count))
				{
					auto p = delta[t - 1][prev_state] * A[prev_state][curr_state];
					if (p > max_prob)
					{
						max_prob  = p;
						max_state = prev_state;
					}
				}

				delta[t][curr_state] = max_prob * B[curr_state][observations[t]];
				psi[t][curr_state]	 = max_state;
			}
		}

		auto max_prob  = 0.f;
		auto max_state = 0;
		for (auto state : std::views::iota(0uz, state_count))
		{
			if (delta[T - 1][state] > max_prob)
			{
				max_prob  = delta[T - 1][state];
				max_state = state;
			}
		}

		path[T - 1] = std::ranges::max_element(delta[T - 1]) - delta[T - 1].begin();

		for (auto t : std::views::iota(0uz, T - 1) | std::views::reverse)
		{
			path[t] = psi[t + 1][path[t + 1]];
		}
	}

	void baum_welch()
	{
	}

	void print()
	{
		std::cout << "A : \n"
				  << format_matrix(A) << std::endl;
		std::cout << "B : \n"
				  << format_matrix(B) << std::endl;
		std::cout << "pi : \n"
				  << format_matrix(pi) << std::endl;
		std::cout << "observations : \n"
				  << format_matrix(observations) << std::endl;
		std::cout << "alpha : \n"
				  << format_matrix(alpha) << std::endl;
		std::cout << "beta : \n"
				  << format_matrix(beta) << std::endl;
		std::cout << "gamma : \n"
				  << format_matrix(gamma) << std::endl;
		std::cout << "xi : \n"
				  << format_matrix(xi) << std::endl;
		std::cout << "delta : \n"
				  << format_matrix(delta) << std::endl;
		std::cout << "psi : \n"
				  << format_matrix(psi) << std::endl;
		std::cout << "path : \n"
				  << format_arr(path) << std::endl;
	}
};

int main()
{
	model<5, 10, 10> hmm;
	hmm.init_A_B_pi();
	hmm.gen_random_observations();

	hmm.forward();
	hmm.backward();
	hmm.init_gamma();
	hmm.init_xi();
	hmm.viterbi();

	hmm.print();

	return 0;
	// TIP See CLion help at <a href="https://www.jetbrains.com/help/clion/">jetbrains.com/help/clion/</a>. Also, you can try interactive lessons for CLion by selecting 'Help | Learn IDE Features' from the main menu.
}
