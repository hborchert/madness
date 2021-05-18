/**
 \file test_vectormacrotask.h
 \brief Tests the \c MacroTaskQ and MacroTask classes
 \ingroup mra

 For more information also see macrotask.h

 The user defines a macrotask (an example is found in test_vectormacrotask.cc), the tasks are
 lightweight and carry only bookkeeping information, actual input and output are stored in a
 cloud (see cloud.h)

 The user-defined MacroTask is derived from MacroTaskIntermediate and must implement the run()
 method. A heterogeneous task queue is possible.

 When running the taskq with Functions make sure the process map is reset before and after the
 task execution:

 	    FunctionDefaults<NDIM>::set_default_pmap(taskq.get_subworld());
	    taskq.run_all(task_list);
		FunctionDefaults<NDIM>::set_default_pmap(universe);

 Also make sure the subworld objects are properly destroyed after the execution

*/

/*
 * for_each i
 *      result[i] = op(arg[i],other_args)
 *
 * for_each i
 *      result[j] = op(arg[i],other_args)
 *
 */



#include <madness/mra/mra.h>
#include <madness/world/cloud.h>
#include <madness/mra/macrotaskq.h>
#include <madness/world/world.h>
#include <madness/world/timing_utilities.h>
#include <madness/mra/macrotaskpartitioner.h>


using namespace madness;
using namespace archive;

struct slater {
    double a=1.0;

    slater(double aa) : a(aa) {}

    template<std::size_t NDIM>
    double operator()(const Vector<double,NDIM> &r) const {
        double x = inner(r,r);
        return exp(-a *sqrt(x));
    }
};

struct gaussian {
    double a;

    gaussian() : a() {};

    gaussian(double aa) : a(aa) {}

    template<std::size_t NDIM>
    double operator()(const Vector<double,NDIM> &r) const {
        double r2 = inner(r,r);
        return exp(-a * r2);//*abs(sin(abs(2.0*x))) *cos(y);
    }
};


template<typename Q>
struct is_vector : std::false_type {
};
template<typename Q>
struct is_vector<std::vector<Q>> : std::true_type {
};

template<typename taskT>
class MacroTask_2G {
    using partitionT = MacroTaskPartitioner::partitionT;

    typedef typename taskT::resultT resultT;
    typedef typename taskT::argtupleT argtupleT;
    typedef Cloud::recordlistT recordlistT;
    taskT task;

public:

    MacroTask_2G(World &world, taskT &task, std::shared_ptr<MacroTaskQ> taskq_ptr = 0)
            : world(world), task(task), taskq_ptr(taskq_ptr) {
        if (taskq_ptr) {
            // for the time being this condition must hold because tasks are
            // constructed as replicated objects and are not broadcast to other ranks
            MADNESS_CHECK(world.id()==taskq_ptr->get_world().id());
        }
    }

    template<typename ... Ts>
    resultT operator()(const Ts &... args) {

        const bool immediate_execution = (not taskq_ptr);
        if (not taskq_ptr) taskq_ptr.reset(new MacroTaskQ(world, world.size()));

        auto argtuple = std::tie(args...);
        static_assert(std::is_same<decltype(argtuple), argtupleT>::value, "type or number of arguments incorrect");

        // partition the argument vector into batches
        auto partitioner=task.partitioner;
        if (not partitioner) partitioner.reset(new MacroTaskPartitioner);
        partitioner->set_nsubworld(world.size());
        partitionT partition = partitioner->partition_tasks(argtuple);

        // store input and output: output being a pointer to a universe function (vector)
        recordlistT inputrecords = taskq_ptr->cloud.store(world, argtuple);
        auto[outputrecords, result] =prepare_output(taskq_ptr->cloud, argtuple);

        // create tasks and add them to the taskq
        MacroTaskBase::taskqT vtask;
        for (const Batch &batch : partition) {
            vtask.push_back(
                    std::shared_ptr<MacroTaskBase>(new MacroTaskInternal(task, batch, inputrecords, outputrecords)));
        }
        taskq_ptr->add_tasks(vtask);

        if (immediate_execution) taskq_ptr->run_all(vtask);

        return result;
    }

private:

    World &world;
    std::shared_ptr<MacroTaskQ> taskq_ptr;

    /// prepare the output of the macrotask: WorldObjects must be created in the universe
    std::pair<recordlistT, resultT> prepare_output(Cloud &cloud, const argtupleT &argtuple) {
        static_assert(is_madness_function<resultT>::value || is_madness_function_vector<resultT>::value);
        resultT result = task.allocator(world, argtuple);
        recordlistT outputrecords;
        if constexpr (is_madness_function<resultT>::value) {
            outputrecords += cloud.store(world, result.get_impl().get()); // store pointer to FunctionImpl
        } else if constexpr (is_vector<resultT>::value) {
            outputrecords += cloud.store(world, get_impl(result));
        } else {
            MADNESS_EXCEPTION("\n\n  unknown result type in prepare_input ", 1);
        }
        return std::make_pair(outputrecords, result);
    }

    class MacroTaskInternal : public MacroTaskIntermediate<MacroTask_2G> {

        typedef decay_tuple<typename taskT::argtupleT> argtupleT;   // removes const, &, etc
        typedef typename taskT::resultT resultT;
        recordlistT inputrecords;
        recordlistT outputrecords;
    public:
        taskT task;

        MacroTaskInternal(const taskT &task, const Batch &batch,
                          const recordlistT &inputrecords, const recordlistT &outputrecords)
                : task(task), inputrecords(inputrecords), outputrecords(outputrecords) {
            static_assert(is_madness_function<resultT>::value || is_madness_function_vector<resultT>::value);
            this->task.batch=batch;
        }


        virtual void print_me(std::string s="") const {
            print("this is task",typeid(task).name(),"with batch", task.batch,"priority",this->get_priority());
        }

        virtual void print_me_as_table(std::string s="") const {
            std::stringstream ss;
            std::string name=typeid(task).name();
            std::size_t namesize=name.size();
            name += std::string(20-namesize,' ');

            ss  << name
                << std::setw(10) << task.batch
                << std::setw(5) << this->get_priority() << "        "
                <<this->stat ;
            print(ss.str());
        }

        void run(World &subworld, Cloud &cloud, MacroTaskBase::taskqT &taskq) {

            const argtupleT argtuple = cloud.load<argtupleT>(subworld, inputrecords);
            const argtupleT batched_argtuple = task.batch.template copy_input_batch(argtuple);

            resultT result_tmp = std::apply(task, batched_argtuple);

            resultT result = get_output(subworld, cloud, argtuple);       // lives in the universe
            if constexpr (is_madness_function<resultT>::value) {
                result_tmp.compress();
                result += result_tmp;
            } else if constexpr(is_madness_function_vector<resultT>::value) {
                compress(subworld, result_tmp);
                resultT tmp1=task.allocator(subworld,argtuple);
                tmp1=task.batch.template insert_result_batch(tmp1,result_tmp);
                result += tmp1;
            } else {
                MADNESS_EXCEPTION("failing result",1);
            }

        };

        resultT get_output(World &subworld, Cloud &cloud, const argtupleT &argtuple) {
            resultT result;
            if constexpr (is_madness_function<resultT>::value) {
                typedef std::shared_ptr<typename resultT::implT> impl_ptrT;
                result.set_impl(cloud.load<impl_ptrT>(subworld, outputrecords));
            } else if constexpr (is_madness_function_vector<resultT>::value) {
                typedef std::shared_ptr<typename resultT::value_type::implT> impl_ptrT;
                std::vector<impl_ptrT> rimpl = cloud.load<std::vector<impl_ptrT>>(subworld, outputrecords);
                result.resize(rimpl.size());
                set_impl(result, rimpl);
            } else {
                MADNESS_EXCEPTION("unknown result type in get_output", 1);
            }
            return result;
        }

    };

};

class MicroTaskBase {
public:
    Batch batch;
    std::shared_ptr<MacroTaskPartitioner> partitioner=0;
    MicroTaskBase() : batch(Batch(_,_,_)),  partitioner(new MacroTaskPartitioner) {}
};

class MicroTask : public MicroTaskBase {
public:
    // you need to define the exact argument(s) of operator() as tuple
    typedef std::tuple<const real_function_3d &, const double &,
            const std::vector<real_function_3d> &> argtupleT;

    using resultT = std::vector<real_function_3d>;

    // you need to define an empty constructor for the result
    // resultT must implement operator+=(const resultT&)
    resultT allocator(World &world, const argtupleT &argtuple) const {
        std::size_t n = std::get<2>(argtuple).size();
        resultT result = zero_functions_compressed<double, 3>(world, n);
        return result;
    }

    resultT operator()(const real_function_3d &f1, const double &arg2,
                       const std::vector<real_function_3d> &f2) const {
        World& world=f1.world();
        real_convolution_3d op=CoulombOperator(world,1.e-4,1.e-5);
        return arg2 * f1 * apply(world,op,f2);
    }
};


class MicroTask1 : public MicroTaskBase{
public:
    // you need to define the result type
    // resultT must implement operator+=(const resultT&)
    typedef real_function_3d resultT;

    // you need to define the exact argument(s) of operator() as tuple
    typedef std::tuple<const real_function_3d &, const double &,
            const std::vector<real_function_3d> &> argtupleT;

    resultT allocator(World &world, const argtupleT &argtuple) const {
        resultT result = real_factory_3d(world).compressed();
        return result;
    }

    resultT operator()(const real_function_3d &f1, const double &arg2,
                       const std::vector<real_function_3d> &f2) const {
        World &world = f1.world();
        auto result = arg2 * f1 * inner(f2, f2);
        return result;
    }
};

class MicroTask2 : public MicroTaskBase{
public:
    // you need to define the result type
    // resultT must implement operator+=(const resultT&)
    typedef std::vector<real_function_3d> resultT;

    // you need to define the exact argument(s) of operator() as tuple
    typedef std::tuple<const std::vector<real_function_3d> &, const double &,
            const std::vector<real_function_3d> &> argtupleT;

    resultT allocator(World &world, const argtupleT &argtuple) const {
        std::size_t n = std::get<2>(argtuple).size();
        resultT result = zero_functions_compressed<double, 3>(world, n);
        return result;
    }

    resultT operator()(const std::vector<real_function_3d>& f1, const double &arg2,
                       const std::vector<real_function_3d>& f2) const {
        World &world = f1[0].world();
        // // won't work because of nested loop over f1
        // if (batch.input[0]==batch.input[1]) {
        //     return arg2 * f1 * inner(f1,f2);
        // }

        // // won't work because result batches are the same as f1 batches
        // return arg2*f2*inner(f1,f1);

        // will work
        return arg2*f1*inner(f2,f2);

    }
};

int check_vector(World& universe, const std::vector<real_function_3d> &ref, const std::vector<real_function_3d> &test,
                        const std::string msg) {
    double norm_ref = norm2(universe, ref);
    double norm_test = norm2(universe, test);
    double error = norm2(universe, ref - test);
    bool success = error/norm_ref < 1.e-10;
    if (universe.rank() == 0) {
        print("norm ref, test, diff", norm_ref, norm_test, error);
        if (success) print("test", msg, " \033[32m", "passed ", "\033[0m");
        else print("test", msg, " \033[31m", "failed \033[0m ");
    }
    return (success) ? 0 : 1;
};
int check(World& universe, const real_function_3d &ref, const real_function_3d &test, const std::string msg) {
    double norm_ref = ref.norm2();
    double norm_test = test.norm2();
    double error = (ref - test).norm2();
    bool success = error/norm_ref < 1.e-10;
    if (universe.rank() == 0) {
                print("norm ref, test, diff", norm_ref, norm_test, error);
        if (success) print("test", msg, " \033[32m", "passed ", "\033[0m");
        else print("test", msg, " \033[31m", "failed \033[0m ");
    }
    return (success) ? 0 : 1;
};

int test_immediate(World& universe, const std::vector<real_function_3d>& v3,
                   const std::vector<real_function_3d>& ref) {
    if (universe.rank() == 0) print("\nstarting immediate execution");
    MicroTask t;
    MacroTask_2G task_immediate(universe, t);
    std::vector<real_function_3d> v = task_immediate(v3[0], 2.0, v3);
    int success=check_vector(universe,ref,v,"test_immediate execution of task");
    return success;
}

int test_deferred(World& universe, const std::vector<real_function_3d>& v3,
                   const std::vector<real_function_3d>& ref) {
    if (universe.rank() == 0) print("\nstarting deferred execution");
    auto taskq = std::shared_ptr<MacroTaskQ>(new MacroTaskQ(universe, universe.size()));
    taskq->set_printlevel(3);
    MicroTask t;
    MacroTask_2G task(universe, t, taskq);
    std::vector<real_function_3d> f2a = task(v3[0], 2.0, v3);
    taskq->print_taskq();
    taskq->run_all();
    taskq->cloud.print_timings(universe);
    taskq->cloud.clear_timings();
    int success=check_vector(universe,ref,f2a,"test_deferred execution of task");
    return success;
}

int test_twice(World& universe, const std::vector<real_function_3d>& v3,
                  const std::vector<real_function_3d>& ref) {
    if (universe.rank() == 0) print("\nstarting Microtask twice (check caching)\n");
    auto taskq = std::shared_ptr<MacroTaskQ>(new MacroTaskQ(universe, universe.size()));
    taskq->set_printlevel(3);
    MicroTask t;
    MacroTask_2G task(universe, t, taskq);
    std::vector<real_function_3d> f2a1 = task(v3[0], 2.0, v3);
    std::vector<real_function_3d> f2a2 = task(v3[0], 2.0, v3);
    taskq->print_taskq();
    taskq->run_all();
    taskq->cloud.print_timings(universe);
    int success=0;
    success += check_vector(universe, ref, f2a1, "taske twice a");
    success += check_vector(universe, ref, f2a2, "taske twice b");
    return success;
}

int test_task1(World& universe, const std::vector<real_function_3d>& v3) {
    if (universe.rank()==0) print("\nstarting Microtask1\n");
    MicroTask1 t1;
    real_function_3d ref_t1 = t1(v3[0], 2.0, v3);
    MacroTask_2G task1(universe,t1);
    real_function_3d ref_t2 = task1(v3[0], 2.0, v3);
    int success = check(universe,ref_t1, ref_t2, "task1 immediate");
    return success;
}

int test_2d_partitioning(World& universe, const std::vector<real_function_3d>& v3) {
    if (universe.rank() == 0) print("\nstarting 2d partitioning");
    auto taskq = std::shared_ptr<MacroTaskQ>(new MacroTaskQ(universe, universe.size()));
    taskq->set_printlevel(3);
    MicroTask2 t;
    auto ref=t(v3,2.0,v3);
    t.partitioner->set_dimension(2);
    MacroTask_2G task(universe, t, taskq);
    std::vector<real_function_3d> f2a = task(v3, 2.0, v3);
    taskq->print_taskq();
    taskq->run_all();
    taskq->cloud.print_timings(universe);
    taskq->cloud.clear_timings();
    int success=check_vector(universe,ref,f2a,"test 2d partitioning");
    return success;
}

int main(int argc, char **argv) {
    madness::World &universe = madness::initialize(argc, argv);
    startup(universe, argc, argv);
    FunctionDefaults<3>::set_thresh(1.e-5);
    FunctionDefaults<3>::set_k(9);
    FunctionDefaults<3>::set_cubic_cell(-20,20);

    int success = 0;

    universe.gop.fence();
    int nworld = universe.size();
    if (universe.rank() == 0) print("creating nworld", nworld, universe.id());

    // TODO: serialize member variables of tasks
    // TODO: pretty-print cloud content/input/output records

    {
        // execution in a taskq, result will be complete only after the taskq is finished
        real_function_3d f1 = real_factory_3d(universe).functor(slater(1.0));
        real_function_3d i2 = real_factory_3d(universe).functor(slater(2.0));
        real_function_3d i3 = real_factory_3d(universe).functor(slater(2.0));
        std::vector<real_function_3d> v2 = {2.0 * f1, i2};
        std::vector<real_function_3d> v3;
        for (int i=0; i<20; ++i) v3.push_back(real_factory_3d(universe).functor(slater(sqrt(double(i)))));

        timer timer1(universe);
        MicroTask t;
        std::vector<real_function_3d> ref = t(v3[0], 2.0, v3);
        timer1.tag("direct execution");

        success+=test_immediate(universe,v3,ref);
        timer1.tag("immediate taskq execution");

        success+=test_deferred(universe,v3,ref);
        timer1.tag("deferred taskq execution");

        success+=test_twice(universe,v3,ref);
        timer1.tag("executing a task twice");

        success+=test_task1(universe,v3);
        timer1.tag("task1 immediate execution");

        success+=test_2d_partitioning(universe,v3);
        timer1.tag("2D partitioning");

        if (universe.rank() == 0) {
            if (success==0) print("\n --> all tests \033[32m", "passed ", "\033[0m\n");
            else print("\n --> all tests \033[31m", "failed \033[0m \n");
        }
    }

    madness::finalize();
    return 0;
}

template<> volatile std::list<detail::PendingMsg> WorldObject<MacroTaskQ>::pending = std::list<detail::PendingMsg>();
template<> Spinlock WorldObject<MacroTaskQ>::pending_mutex(0);

//template <> volatile std::list<detail::PendingMsg> WorldObject<WorldContainerImpl<long, MacroTask, madness::Hash<long> > >::pending = std::list<detail::PendingMsg>();
//template <> Spinlock WorldObject<WorldContainerImpl<long, MacroTask, madness::Hash<long> > >::pending_mutex(0);

template<> volatile std::list<detail::PendingMsg> WorldObject<WorldContainerImpl<long, std::vector<unsigned char>, madness::Hash<long> > >::pending = std::list<detail::PendingMsg>();
template<> Spinlock WorldObject<WorldContainerImpl<long, std::vector<unsigned char>, madness::Hash<long> > >::pending_mutex(
        0);
