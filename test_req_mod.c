#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/printk.h>
#include <linux/kthread.h>
#include <linux/sched.h>

static char *test_driver = NULL;
module_param(test_driver, charp, S_IRUGO);
MODULE_PARM_DESC(test_driver, "Test driver to load with request_module()");

#define MAX_TRYS 49
int rets_sync[MAX_TRYS];

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
	rets_sync[*i] = request_module("%s", test_driver);
	pr_info("Ran thread %d\n", *i);
	wake_up_module_loader();

	spin_lock(&lock);
	done++;
	if (done == MAX_TRYS-1) {
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

	for (i=0; i < MAX_TRYS-1; i++)
		pr_info("Sync thread %d return status: %d\n",
			i, rets_sync[i]);
}

static void try_requests(void)
{
	unsigned int i;
	char name[12];
	bool any_error = false;

	for (i=0; i < MAX_TRYS-1; i++) {
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
		if (done >= MAX_TRYS-1) {
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

	if (!test_driver) {
		pr_info("Must load with the test_driver module parameter\n");
		return -EINVAL;
	}

	pr_info("Testing request_module()!\n");
	pr_info("Test driver to load: %s\n", test_driver);
	pr_info("Number of threads to run: %d\n", MAX_TRYS-1);
	pr_info("Thread IDs will range from 0 - %d\n", MAX_TRYS-2);

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

	for (i=0; i < MAX_TRYS-1; i++) {
		if (tasks_sync[i]) {
			pr_info("Stopping still-running thread %i\n", i);
			kthread_stop(tasks_sync[i]);
		}
	}
}

module_init(test_request_module_init)
module_exit(test_request_module_exit)
MODULE_LICENSE("GPL");
