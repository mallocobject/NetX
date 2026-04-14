#include "type_list.hpp"
#include <type_traits>

#define link(x) x->void
#define node(x) auto (*)(x)

template <char ID> struct Node
{
	constexpr inline static char id = ID;
};

using A = Node<'A'>;
using B = Node<'B'>;
using C = Node<'C'>;
using D = Node<'D'>;
using E = Node<'E'>;

template <typename Node>
concept Vertex = requires { Node::id; };

// template <typename Node>
// concept Vertex = requires(Node n) { n.id; };

template <Vertex F, Vertex T> struct Edge
{
	using From = F;
	using To = T;
};

template <typename Node = void>
	requires(Vertex<Node> || std::is_void_v<Node>)
struct EdgeTrait
{
	template <typename Edge>
	using IsFrom = std::is_same<typename Edge::From, Node>;
	template <typename Edge> using IsTo = std::is_same<typename Edge::To, Node>;
	template <typename Edge> using GetFrom = typename Edge::From;
	template <typename Edge> using GetTo = typename Edge::To;
};

template <typename Link, TL Out = TypeList<>> struct Chain;

template <Vertex F, TL Out> struct Chain<auto (*)(F)->void, Out>
{
	using From = F;
	using type = Out;
};

template <Vertex F, typename T, TL Out> struct Chain<auto (*)(F)->T, Out>
{
	using From = F;
	using To = typename Chain<T, Out>::From;

	using type =
		typename Chain<T, typename Out::template append<Edge<From, To>>>::type;
};

template <typename... Chains> struct Graph
{
	using Edges = Unique_t<Concat_t<typename Chains::type...>>;
};

using g = Graph<Chain<link(node(A)->node(B)->node(C)->node(D))>>;

static_assert(
	std::is_same_v<g::Edges, TypeList<Edge<A, B>, Edge<B, C>, Edge<C, D>>>);
