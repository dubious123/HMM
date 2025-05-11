#include <iostream>
#include <array>
#include <ranges>
#include <algorithm>
#include "common.h"

using t_state		= uint8;
using t_observation = uint8;

// number of state.
constexpr auto N = 5;
// number of observation.
constexpr auto M = 10;
// sequence length
constexpr auto T = 10;
// transition probability distribution
float A[N][N];
// observation symbol probability distribution
float B[N][M];
// initial state distribution
float pi[N];
// observation sequence
uint8 O[T];
// state sequence
uint8 Q[T];

// lambda = (A, B, pi)

// P1 : compute P(O | lambda) -> allows us to choose the best-match model

// p2 : given O, find optimal Q -> uncover hidden part of the model

// p3 : given O, find optimal lambda (A, B, pi) -> train the model

// p3 (find model) -> p2 (understand physical meaning of the model state) -> p1 (score each model)

// lambda
struct model
{
	std::array<std::array<float32, N>, N> A;
	std::array<std::array<float32, M>, N> B;
	std::array<float32, N>				  pi;
};

int main()
{
	// alpha[t][state] : t에서 상태가 state일 확률 * 해당 상태에서 관측 O_t가 나올 확률
	// 0 ~ t 까지 observation이 주어졌을 때 t에서 상태 s 에 있을 확률
	// t 에서 s 에 있을 확률 = t - 1에서 모든 s에서의 alpha * 전이확률 의 합 * observation 확률
	auto alpha = std::array<std::array<float32, N>, T> {};

	// Forward
	// O(N^2 * T)
	for (auto state : std::views::iota(0, N))
		alpha[0][state] = pi[state] * B[state][O[0]];

	for (auto t : std::views::iota(1, T))
	{
		for (auto curr_state : std::views::iota(0, N))
		{
			auto sum = 0.f;
			for (auto prev_state : std::views::iota(0, N))
			{
				sum += alpha[t - 1][prev_state] * A[prev_state][curr_state];
			}

			alpha[t][curr_state] = sum * B[curr_state][O[t]];
		}
	}

	// t 에서 s에 있을 때 0(t+1), O(t+2), ... , O(T) 가 발생할 확률
	// 마지막은 1, (이후에 발생될것이 없음)
	auto beta = std::array<std::array<float32, N>, T> {};

	for (auto state : std::views::iota(0, N))
	{
		beta[T - 1][state] = 1.f;
	}

	// Backward
	// O(N^2 * T)
	for (auto t : std::views::iota(0, T - 1) | std::views::reverse)
	{
		for (auto curr_state : std::views::iota(0, N))
		{
			for (auto next_state : std::views::iota(0, N))
			{
				beta[t][curr_state] += beta[t + 1][next_state] * A[curr_state][next_state] * B[next_state][O[t + 1]];
			}
		}
	}

	// sol to p1 : sum over alpha
	{
		auto p = std::ranges::fold_left_first(alpha[T - 1], std::plus {}).value();
	}

	// or sum over beta
	{
		auto p = std::ranges::fold_left_first(std::views::iota(0, N) | std::views::transform([&](auto state) { return pi[state] * B[state][O[0]] * beta[0][state]; }), std::plus {}).value();
	}

	// prob of being at state s on time t
	auto gamma = std::array<std::array<float32, N>, T> {};
	{
		// p(O|lambda), sol to p1
		auto p = std::ranges::fold_left_first(alpha[T - 1], std::plus {}).value();
		for (auto t : std::views::iota(0, T))
		{
			for (auto state : std::views::iota(0, N))
			{
				gamma[t][state] = alpha[t][state] * beta[t][state] / p;
			}
		}

		// sum of gamma[t] == 1;
	}
	// ξ
	// xi(curr, next) gamma(curr) * A[curr][next] * gamma(next) -> X
	// xi(t, curr, next) = alpha[t][curr] * A[curr][next] * B[next][O[t+1] * beta[t+1][next]
	auto xi = std::array<std::array<std::array<float32, N>, N>, T> {};
	{
		auto p = std::ranges::fold_left_first(alpha[T - 1], std::plus {}).value();
		for (auto t : std::views::iota(0, T - 1))
		{
			for (auto curr_state : std::views::iota(0, N))
			{
				for (auto next_state : std::views::iota(0, N))
				{
					xi[t][curr_state][next_state] = alpha[t][curr_state] * A[curr_state][next_state] * B[next_state][O[t + 1]] * beta[t + 1][next_state] / p;
				}
			}
		}
	}

	// viterbi
	{
		// δ
		// t까지 관측이 진행되었을 때 다음 상태에 도달하는 가장 높은 경로의 확률
		// alpha : t 에서 s일 총 확률
		// delta : t 에서 s일 가장 적절한 경로의 확률
		auto delta = std::array<std::array<float32, N>, T> {};

		// ψ
		// t에서 다음 상태에 도달하기 직전 최고 확률 경로의 직전 상태
		// psi[t][j] = argmax_i (delta[t-1][i] * A[i][j])
		auto psi = std::array<std::array<t_state, N>, T> {};

		// 최적의 state array
		auto path = std::array<t_state, N> {};

		for (auto s : std::views::iota(0, N))
		{
			delta[0][s] = pi[s] * B[s][O[0]];
			psi[0][s]	= 0;	// dummy
		}

		for (auto t : std::views::iota(1, T))
		{
			for (auto curr_state : std::views::iota(0, N))
			{
				auto max_prob  = 0.f;
				auto max_state = 0;
				for (auto prev_state : std::views::iota(0, N))
				{
					auto p = delta[t - 1][prev_state] * A[prev_state][curr_state];
					if (p > max_prob)
					{
						max_prob  = p;
						max_state = prev_state;
					}
				}

				delta[t][curr_state] = max_prob * B[curr_state][O[t]];
				psi[t][curr_state]	 = max_state;
			}
		}

		// backtracking
		{
			auto max_prob  = 0.f;
			auto max_state = 0;
			for (auto state : std::views::iota(0, N))
			{
				if (delta[T - 1][state] > max_prob)
				{
					max_prob  = delta[T - 1][state];
					max_state = state;
				}
			}

			path[T - 1] = std::ranges::max_element(delta[T - 1]) - delta[T - 1].begin();

			for (auto t : std::views::iota(0, T - 1) | std::views::reverse)
			{
				path[t] = psi[t + 1][path[t + 1]];
			}
		}
	}

	auto lang = "C++";
	std::cout << "Hello and welcome to C++" << std::endl;

	for (int i = 1; i <= 5; i++)
	{
		std::cout << "i = " << i << std::endl;
	}
	return 0;
	// TIP See CLion help at <a href="https://www.jetbrains.com/help/clion/">jetbrains.com/help/clion/</a>. Also, you can try interactive lessons for CLion by selecting 'Help | Learn IDE Features' from the main menu.
}
