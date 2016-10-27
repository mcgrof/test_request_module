#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/printk.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/fs.h>

static char *test_driver = NULL;
module_param(test_driver, charp, S_IRUGO);
MODULE_PARM_DESC(test_driver, "Test driver to load with request_module()");

static char *test_fs = NULL;
module_param(test_fs, charp, S_IRUGO);
MODULE_PARM_DESC(test_fs, "Test filesystem check()");

#define NUM_THREADS 80
unsigned int num_threads = NUM_THREADS;

#define MAX_TRYS NUM_THREADS + 1

int rets_sync[MAX_TRYS];
struct file_system_type *fs_sync[MAX_TRYS];

struct task_struct *wake_up_task;
struct task_struct *tasks_sync[MAX_TRYS];

unsigned int vals[MAX_TRYS];

unsigned int done;
static DEFINE_SPINLOCK(lock);
static DECLARE_COMPLETION(kthreads_done);

static void wake_up_module_loader(void)
{
	if (wake_up_task)
		wake_up_process(wake_up_task);
}

static int run_request(void *data)
{
	unsigned int *i = data;

	wake_up_module_loader();

	if (test_driver)
		rets_sync[*i] = request_module("%s", test_driver);
	else if (test_fs) {
		if (*i % 2 == 0)
			rets_sync[*i] = request_module("%s", test_fs);
		else
			fs_sync[*i] = get_fs_type(test_fs);
	}

	pr_info("Ran thread %d\n", *i);
	wake_up_module_loader();

	spin_lock(&lock);
	done++;
	if (done == num_threads) {
		pr_info("Done: %d threads have all run now\n", done);
		pr_info("Last thread to run: %d\n", *i);
		complete(&kthreads_done);
	}
	spin_unlock(&lock);

	tasks_sync[*i] = NULL;

	wake_up_module_loader();

	return 0;
}

static void tally_up_work(void)
{
	unsigned int i;

	pr_info("Results:\n");

	for (i=0; i < num_threads; i++) {
		if (test_driver)
			pr_info("Sync thread %d return status: %d\n",
				i, rets_sync[i]);
		else if (test_fs) {
			if (i % 2 == 0)
				pr_info("Sync thread %d return status: %d\n",
					i, rets_sync[i]);
			else
				pr_info("Sync thread %d fs: %s\n",
					i, fs_sync[i] ? test_fs : "NULL");
		}
	}
}

static void try_requests(void)
{
	unsigned int i;
	char name[12];
	bool any_error = false;

	for (i=0; i < num_threads; i++) {
		vals[i] = i;
		sprintf(name, "test-run-%d", i);
		wake_up_module_loader();
		tasks_sync[i] = kthread_run(run_request, &vals[i], name);
		wake_up_module_loader();
		if (IS_ERR(tasks_sync[i])) {
			pr_info("Setting up thread %i failed\n", i);
			tasks_sync[i] = NULL;
			any_error = true;
		} else
			pr_info("Kicked off thread %i\n", i);
	}

	if (!any_error) {
		pr_info("No errors were found while initializing threads\n");
		wait_for_completion(&kthreads_done);
		tally_up_work();
	} else
		pr_info("At least one thread failed to start\n");
}

static int run_wake_up(void *data)
{
	unsigned int i = 0;

	while (true) {
		spin_lock(&lock);
		if (done >= num_threads) {
			spin_unlock(&lock);
			pr_info("Wake up thread no longer needed, on count %d\n", i);
			break;
		}
		spin_unlock(&lock);

		i++;

		if (i > 1400000) {
			pr_info("Bailing on wake up thread, upper bound reached\n");
			break;
		}
#ifdef wake_up_modules
		wake_up_modules();
#endif
	}
	wake_up_task = NULL;
	return 0;
}

static int setup_mod_waker(void)
{
	wake_up_task = kthread_run(run_wake_up, NULL, "test-run-wakeup");
	if (IS_ERR(wake_up_task)) {
		pr_info("Failed to set up wake up task\n");
		return -ENOMEM;
	}
	return 0;
}

static int __init test_request_module_init(void)
{
	int r;

	if (!test_driver && !test_fs) {
		pr_err("Must load with the either the test_driver or test_fs module parameter\n");
		return -EINVAL;
	}
	if (test_driver && test_fs) {
		pr_err("Module parameteres test_driver and test_fs are mutually exclusive, pick on");
		return -EINVAL;
	}

	pr_info("Testing request_module()!\n");
	if (test_driver)
		pr_info("Test driver to load: %s\n", test_driver);
	if (test_fs)
		pr_info("Test filesystem to load: %s\n", test_fs);
	pr_info("Number of threads to run: %d\n", num_threads);
	pr_info("Thread IDs will range from 0 - %d\n", num_threads - 1);

	r = setup_mod_waker();
	if (r)
		return r;

	try_requests();

	return 0;
}

static void __exit test_request_module_exit(void)
{
	unsigned int i;
	pr_info("Ending request_module() tests\n");

	if (wake_up_task) {
		pr_info("Trying to stop wake up task\n");
		kthread_stop(wake_up_task);
	}

	for (i=0; i < num_threads; i++) {
		if (tasks_sync[i]) {
			pr_info("Stopping still-running thread %i\n", i);
			kthread_stop(tasks_sync[i]);
		}
		if (fs_sync[i])
			module_put(fs_sync[i]->owner);
	}
}

module_init(test_request_module_init)
module_exit(test_request_module_exit)
MODULE_LICENSE("GPL");
