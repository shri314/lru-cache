#include <list>
#include <unordered_map>
#include <utility>

template <typename key_t /*primary key type*/, typename value_t /*primary value type*/,
          template <class...> class map_t = std::unordered_map /*std::map, std::unordered_map, or a map type alias with your allocator*/,
          template <class...> class list_t = std::list /*some list type alias with your allocator*/>
class lru_map
{
private:
   struct seq_node_t;
   struct assoc_node_t;

   using seq_store_t = list_t<seq_node_t>;
   using assoc_store_t = map_t<key_t, assoc_node_t>;

   struct seq_node_t
   {
      typename assoc_store_t::const_iterator m_assoc_iter;
   };

   struct assoc_node_t
   {
      template <class compat_value_t>
      assoc_node_t(compat_value_t&& value, typename seq_store_t::const_iterator seq_iter)
         : m_value(std::forward<compat_value_t>(value))
         , m_seq_iter(seq_iter)
      {
      }

      assoc_node_t(assoc_node_t&&) = default;
      assoc_node_t& operator=(const assoc_node_t&) = default;
      assoc_node_t& operator=(assoc_node_t&&) = default;
      ~assoc_node_t() = default;

      value_t m_value;
      typename seq_store_t::const_iterator m_seq_iter;
   };

private:
   size_t m_capacity;
   seq_store_t m_seq_store;
   assoc_store_t m_assoc_store;

public:
   explicit lru_map(size_t capacity)
      : m_capacity(capacity)
   {
      // m_assoc_store.reserve(capacity); -- call when supported
   }

private:
   template <typename compat_key_t, typename compat_value_t>
   void evict_put(compat_key_t&& k, compat_value_t&& v)
   {
      // relocate evictable element's sequence to front
      if (m_seq_store.size() > 1)
      {
         auto last = m_seq_store.end();
         m_seq_store.splice(m_seq_store.begin(), m_seq_store, --last);
      }

      // extract the node to evict (now at front), and overwrite it
      auto&& nh = m_assoc_store.extract(m_seq_store.front().m_assoc_iter);
      nh.key() = std::forward<compat_key_t>(k);
      nh.mapped() = assoc_node_t{std::forward<compat_value_t>(v), m_seq_store.begin()};
      m_seq_store.front().m_assoc_iter = m_assoc_store.insert(std::move(nh)).position;
   }

public:
   template <typename compat_key_t, typename compat_value_t>
   void put(compat_key_t&& k, compat_value_t&& v)
   {
      if (auto e = get(k); !e)
      {
         if (m_assoc_store.size() == m_capacity)
         {
            evict_put(std::forward<compat_key_t>(k), std::forward<compat_value_t>(v));
         }
         else
         {
            m_seq_store.push_front({});
            m_seq_store.front().m_assoc_iter = m_assoc_store.emplace(std::forward<compat_key_t>(k), assoc_node_t{std::forward<compat_value_t>(v), m_seq_store.begin()}).first;
         }
      }
      else
      {
         *e = std::forward<compat_value_t>(v);
      }
   }

   template <typename compat_key_t>
   value_t* get(const compat_key_t& k)
   {
      if (auto&& x = m_assoc_store.find(k); x != m_assoc_store.end())
      {
         // relocate looked up element's seq to front
         if (x->second.m_seq_iter != m_seq_store.begin())
            m_seq_store.splice(m_seq_store.begin(), m_seq_store, x->second.m_seq_iter);

         return &x->second.m_value;
      }

      return nullptr;
   }

   template <typename ostream_t>
   friend ostream_t& operator<<(ostream_t& os, const lru_map& rhs)
   {
      bool sep = 0;
      for (auto&& x : rhs.m_seq_store)
      {
         if (sep)
            os << ',';
         os << "{" << x.m_assoc_iter->first << "," << x.m_assoc_iter->second.m_value << "}";
         sep = true;
      }

      return os;
   }
};

/////////////////////////////////////////////////////////////

#include <cassert>
#include <iostream>
#include <sstream>

template <class TestObj, class Str>
void check(TestObj& t, Str expected)
{
   std::ostringstream oss;
   oss << t;

   auto&& actual = oss.str();

   bool result = (actual == expected);

   std::cout << "result: " << (result ? "pass" : "fail") << "\n";
   std::cout << "  actual:   [" << actual << "]\n";
   std::cout << "  expected: [" << expected << "]\n";

   assert(result);
}

void test_basic()
{
   lru_map<int, int> L(4);
   check(L, "");

   L.put(10, 100);
   check(L, "{10,100}");

   L.put(20, 200);
   check(L, "{20,200},{10,100}");

   L.put(30, 300);
   check(L, "{30,300},{20,200},{10,100}");

   L.put(40, 400);
   check(L, "{40,400},{30,300},{20,200},{10,100}");

   L.put(50, 500);
   check(L, "{50,500},{40,400},{30,300},{20,200}");

   {
      auto e = L.get(40);
      assert(e != nullptr);
      assert(*e == 400);
      check(L, "{40,400},{50,500},{30,300},{20,200}");
   }

   {
      auto e = L.get(99);
      assert(e == nullptr);
      check(L, "{40,400},{50,500},{30,300},{20,200}");
   }

   L.put(30, 301);
   check(L, "{30,301},{40,400},{50,500},{20,200}");
}

/////////////////////////////////////////////////////////////

#include <chrono>
#include <random>
//#include <map>

template <class LRUMap_t, class Vec>
void do_work(LRUMap_t& L, Vec& V)
{
   for (auto r : V)
   {
      L.put(r, r);
   }
}

void test_perf(int capacity, int put_requests, int mean_begin, int mean_end, int deviation)
{
   std::vector<int> V;
   {
      std::random_device rd;
      std::mt19937 gen(rd());

      for (int mean = mean_begin; mean < mean_end; ++mean)
      {
         std::normal_distribution<> dist(mean, deviation);

         int r = dist(gen);
         if (r >= mean_begin && r <= mean_end)
            V.push_back(r);

         if (V.size() >= put_requests)
            break;
      }
   }

   {
      // lru_map<int, int, std::map> L(capacity);
      lru_map<int, int, std::unordered_map> L(capacity);

      std::cout << "----------------------------------------\n";
      auto start = std::chrono::high_resolution_clock::now();

      do_work(L, V);

      auto end = std::chrono::high_resolution_clock::now();
      auto elapsed_seconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
      std::cout << elapsed_seconds.count() << "\n";
      std::cout << "----------------------------------------\n";
   }
}

/////////////////////////////////////////////////////////////

int main()
{
   test_basic();

   test_perf(10000, 1000000000, 0, 100000000, 4);
}
