#ifndef SCHED_H
#define SCHED_H

#include "tasks.h"
#include "event.h"
#include <vector>
#include <queue>
#include <algorithm>

typedef unsigned long simtime_t;

class Job {
  protected:
    const Task  &task;
    simtime_t release;
    simtime_t cost;
    simtime_t allocation;//已分配的执行时间
    simtime_t seqno;//工作序列

  public:
    Job(const Task &tsk,
        simtime_t relt = 0,
        unsigned long sequence_no = 1,
        simtime_t cost = 0);

    const Task& get_task() const { return task; }
    simtime_t get_release()  const { return release; }
    simtime_t get_deadline() const { return release + task.get_deadline(); }
    simtime_t get_cost() const { return cost; }
    simtime_t get_allocation() const { return allocation; }
    unsigned long get_seqno() const { return seqno; }

    void set_release(simtime_t release)
    {
        this->release = release;
    }

    void set_allocation(simtime_t allocation)
    {
        this->allocation = allocation;
    }

    void increase_allocation(simtime_t service_time)
    {
        allocation += service_time;
    }

    bool is_complete() const
    {
        return allocation >= cost;
    }

    simtime_t remaining_demand() const
    {
        return cost - allocation;
    }

    void init_next(simtime_t cost = 0, simtime_t inter_arrival_time = 0);//初始化下一个任务实例

    // callbacks 回收
    virtual void completed(simtime_t when, int proc) {};
};

template <typename Job, typename SimJob>//模板类
class ScheduleSimulationTemplate
{
  public:
    virtual void simulate_until(simtime_t end_of_simulation) = 0;

    virtual void add_release(SimJob *job) = 0;
    virtual void add_ready(Job *job) = 0;
};

class SimJob;

typedef ScheduleSimulationTemplate<Job, SimJob> ScheduleSimulation;

class SimJob : public Job, public Event<simtime_t>
{
  private:
    ScheduleSimulation* sim;

  public:
    SimJob(Task& tsk, ScheduleSimulation* s = NULL) : Job(tsk), sim(s) {};

    void set_simulation(ScheduleSimulation* s) { sim = s; }
    ScheduleSimulation* get_sim() { return sim; }

    void fire(const simtime_t &time)
    {
        sim->add_ready(this);
    }
};

template <typename SimJob, typename Task>
class PeriodicJobSequenceTemplate : public SimJob//周期性工作序列
{
  public:
    PeriodicJobSequenceTemplate(Task& tsk) : SimJob(tsk) {};
    virtual ~PeriodicJobSequenceTemplate() {};

    // simulator callback
    virtual void completed(simtime_t when, int proc);
};

typedef PeriodicJobSequenceTemplate<SimJob, Task> PeriodicJobSequence;

class EarliestDeadlineFirst {//EDF
  public:
    bool operator()(const Job* a, const Job* b)//判断job b是否需要执行操作
    {
        if (a && b)
            return a->get_deadline() > b->get_deadline();
        else if (b && !a)
            return true;
        else
            return false;
    }
};

// periodic job sequence

template <typename Job>
class ProcessorTemplate
{
  private:
    Job*      scheduled;

  public:
    ProcessorTemplate() : scheduled(NULL) {}

    Job* get_scheduled() const { return scheduled; };
    void schedule(Job* new_job) { scheduled = new_job; }

    void idle() { scheduled = NULL; }

    bool advance_time(simtime_t delta)
    {
        if (scheduled)
        {
            scheduled->increase_allocation(delta);
            return scheduled->is_complete();
        }
        else
            return false;
    }
};

typedef ProcessorTemplate<Job> Processor;

template <typename JobPriority, typename Processor>
class PreemptionOrderTemplate
{
  public:
    bool operator()(const Processor& a, const Processor& b)//返回更高的优先级工作
    {
        JobPriority higher_prio;
        return higher_prio(a.get_scheduled(), b.get_scheduled());
    }
};

typedef std::priority_queue<Timeout<simtime_t>,
                            std::vector<Timeout<simtime_t> >,
                            std::greater<Timeout<simtime_t> >
                             > EventQueue;//到达事件队列（优先级队列）

template <typename JobPriority>
class GlobalScheduler : public ScheduleSimulation//全局调度
{
    typedef std::priority_queue<Job*,
                                std::vector<Job*>,
                                JobPriority > ReadyQueue;//就绪队列

  private:
    EventQueue events;//事件队列
    ReadyQueue pending;//就绪队列
    simtime_t  current_time;

    Processor* processors;//处理器
    int num_procs;//处理器数量

    JobPriority                   lower_prio;
    PreemptionOrderTemplate<JobPriority, Processor>  first_to_preempt;

    Event<simtime_t> dummy;

    bool aborted;

  private:

    void advance_time(simtime_t until)
    {
        simtime_t last = current_time;

        current_time = until;

        // 1) advance time until next event (job completion or event)
        for (int i = 0; i < num_procs; i++)
            if (processors[i].advance_time(current_time - last))//看各处理器中的job是否完成
            {
                // process job completion
                Job* sched = processors[i].get_scheduled();
                processors[i].idle();
                // notify simulation callback
                job_completed(i, sched);
                // nofity job callback
                sched->completed(current_time, i);
            }

        // 2) process any pending events
        while (!events.empty())
        {
            const Timeout<simtime_t>& next_event = events.top();

            if (next_event.time() <= current_time)
            {
                next_event.event().fire(current_time);
                events.pop();
            }
            else
                // no more expired events
                break;
        }

        // 3) process any required preemptions
        bool all_checked = false;
        while (!pending.empty() && !all_checked)
        {
            Job* highest_prio = pending.top();
            Processor* lowest_prio_proc;

            lowest_prio_proc = std::min_element(processors,
                                                processors + num_procs,
                                                first_to_preempt);
            Job* scheduled = lowest_prio_proc->get_scheduled();

            if (lower_prio(scheduled, highest_prio))//若正在执行的最低优先级工作比就绪队列中最高级的低，则发生抢占
            {
                // do a preemption
                pending.pop();


                // schedule
                lowest_prio_proc->schedule(highest_prio);

                // notify simulation callback
                job_scheduled(lowest_prio_proc - processors,
                              scheduled,
                              highest_prio);
                if (scheduled && !scheduled->is_complete())
                    // add back into the pending queue
                    pending.push(scheduled);

                // schedule job completion event
                Timeout<simtime_t> ev(highest_prio->remaining_demand() +
                                      current_time,
                                      &dummy);
                events.push(ev);
            }
            else
                all_checked = true;
        }
    }

  public:
    GlobalScheduler(int num_procs)
    {
        aborted = false;
        current_time = 0;
        this->num_procs = num_procs;
        processors = new Processor[num_procs];
    }

    virtual ~GlobalScheduler()
    {
        delete [] processors;
    }

    simtime_t get_current_time() { return current_time; }

    void abort() { aborted = true; }

    void simulate_until(simtime_t end_of_simulation)
    {
        while (current_time <= end_of_simulation &&//进行到最近一个事件发生的时刻
               !aborted &&
               !events.empty()) {
            simtime_t next = events.top().time();
            advance_time(next);
        }
    }

    // Simulation event callback interface
    virtual void job_released(Job *job) {};
    virtual void job_completed(int proc,
                               Job *job) {};
    virtual void job_scheduled(int proc,
                               Job *preempted,
                               Job *scheduled) {};

    // ScheduleSimulation interface
    void add_release(SimJob *job)
    {
        if (job->get_release() >= current_time)
        {
            // schedule future release
            Timeout<simtime_t> rel(job->get_release(), job);
            events.push(rel);
        }
        else
            add_ready(job);
    }

    // ScheduleSimulation interface
    void add_ready(Job *job)
    {
        // release immediately
        pending.push(job);
        // notify callback
        job_released(job);
    }

};



void run_periodic_simulation(ScheduleSimulation& sim,
                             TaskSet& ts,
                             simtime_t end_of_simulation);


#endif
