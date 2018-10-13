#include "sqmw.hpp"

namespace ThreadPool
{
// SQMW implementation
// public:

SQMW::SQMW()
  : SQMW(std::thread::hardware_concurrency(),
         std::thread::hardware_concurrency())
{
}

SQMW::SQMW(std::size_t pool_size)
  : SQMW(pool_size, pool_size)

{
}

SQMW::SQMW(std::size_t pool_size, std::size_t max_pool_size)
  : _waiting_threads(0)
  , _working_threads(0)
  , _pool_size(pool_size)
  , _max_pool_size(max_pool_size)
  , _hooks(nullptr)
{
  this->start_pool();
}

SQMW::~SQMW()
{
  this->stop();
  this->clean();
}

void SQMW::stop()
{
  // Should stop also call clean and stop the threads ?
  std::lock_guard<std::mutex> lock(this->_tasks_lock);
  this->_stop = true;
  this->_cv_variable.notify_all();
}

bool SQMW::is_stop() const
{
  return this->_stop;
}

std::size_t SQMW::threads_available() const
{
  return this->_waiting_threads.load();
}

std::size_t SQMW::threads_working() const
{
  return this->_working_threads.load();
}

void SQMW::register_hooks(std::shared_ptr<Hooks> hooks)
{
  _hooks = hooks;
}

// SQMW implementation
// private:

void SQMW::start_pool()
{
  for (std::size_t i = 0; i < this->_pool_size; i++)
    this->add_worker();
}

void SQMW::clean()
{
  for (auto& t : _pool)
    if (t.joinable())
    {
      CALL_HOOK_POOL(on_worker_die);
      t.join();
    }
}

void SQMW::add_worker(std::size_t nb_task)
{
  // Instantiate a worker and emplace it in the pool.
  Worker w(this);
  _pool.emplace_back(w, nb_task);
}

void SQMW::check_spawn_single_worker()
{
  // Check if we are allowed to spawn a worker
  if (this->_max_pool_size > this->_pool_size)
    // Check if we have space to spawn a worker, and if it is valuable.
    if (this->_working_threads.load() + this->_waiting_threads.load() <
        this->_max_pool_size)
    {
      CALL_HOOK_POOL(on_worker_add);
      this->add_worker(1);
    }
}

// Worker implementation
// public:

SQMW::Worker::Worker(SQMW* pool)
  : _pool(pool)
{
}

void SQMW::Worker::operator()(std::size_t nb_task)
{
  for (std::size_t i = 0; i != nb_task || nb_task == 0; i++)
  {
    // Thread is waiting
    _pool->_waiting_threads += 1;
    std::unique_lock<std::mutex> lock(_pool->_tasks_lock);
    _pool->_cv_variable.wait(
      lock, [&] { return _pool->_stop || !_pool->_tasks.empty(); });

    // Pool is stopped, discard task and exit
    if (_pool->_stop)
      return;

    CALL_HOOK_WORKER(pre_task_hook);

    _pool->_waiting_threads -= 1;
    _pool->_working_threads += 1;

    // Fetch task
    std::packaged_task<void()> task = std::move(_pool->_tasks.front());
    _pool->_tasks.pop();

    // Release lock and exec task
    lock.unlock();
    task();

    CALL_HOOK_WORKER(post_task_hook);
    // Task done
    _pool->_working_threads -= 1;
  }
}
} // namespace ThreadPool
