Performance Comparison: HFT System on MacBook Air vs 16‑Core AMD EPYC Server
Overview
This report compares the performance of a custom high-frequency trading (HFT) exchange simulator on two different machines: a MacBook Air (Docker container) and a 16-core AMD EPYC 4564P server. The system under test is a custom-built exchange simulator with an in-memory order book supporting order additions, modifications, cancellations, and trade matching. Both environments processed an identical sequence of ~510k order events (replayed from IEX DEEP+ market data logs) to evaluate latency and throughput. Key performance metrics (per-event latency distribution and total throughput) were measured and are presented below. The goal is to assess how a laptop-class machine compares to a server-grade processor for this HFT workload, and to discuss expected benefits of scaling to even higher core counts.
Architecture Summary
The HFT exchange simulator is designed with performance in mind, using efficient data structures and concurrency patterns. Its notable architectural features include:
Order Book Structure: Each instrument’s order book maintains two sorted maps (bids and asks) backed by plf::hive containers, which provide efficient memory management and stable iterators for fast inserts, deletes, and order matching.


Isolated Ticker Threads: The system spawns one dedicated thread per ticker symbol. Each order book runs on its own thread, ensuring that order matching for different tickers occurs in parallel without lock contention between books.


Lock-Free Message Queues: Incoming order messages (parsed from the log feed) are dispatched to the appropriate order book thread via lock-free concurrent queues. This minimizes synchronization overhead when handing off messages to the matching engine threads.


Structured Logging: All significant events—price level updates, trades, cancellations—are logged in a structured format. This logging provides traceability and debugging insight, though it introduces some I/O overhead.


Replay of Real Market Data: The workload is a replay of IEX DEEP+ message logs, providing realistic market behavior with a mix of order additions, modifications, and cancellations. This ensures the performance measurements reflect a real-world HFT scenario.


Benchmark Results
The HFT system was benchmarked on the two platforms under identical conditions, processing a total of 509,736 events in each run. The MacBook Air test was run inside a Docker container (on macOS), whereas the AMD EPYC test ran on a dedicated Linux server. Both runs produced comprehensive latency statistics and throughput measurements, summarized in Table 1.
Table 1: Performance Metrics on Two Platforms
Metric
AMD EPYC 4564P (16-core server)
MacBook Air (Docker container)
Events Processed
509,736
509,736
Wall-Clock Time (s)
1.70418
1.78787
Throughput (events/s)
299,108
285,108
Avg Latency per Event (μs)
3.18522
3.32959
Min / Max Latency (μs)
0 / 76
0 / 5,134
95th Percentile Latency (μs)
3
3

Figure 1: Throughput comparison between the 16‑core AMD EPYC server and the MacBook Air. The EPYC platform sustained ~299,108 events per second, slightly outperforming the MacBook Air’s ~285,108 events per second (higher is better). The difference (~5% greater throughput on EPYC) reflects the server’s faster CPU and lack of virtualization overhead.

<p align="center">
  <img src="imgs/fig1.png" alt="fig1" width="500">
</p>

As shown above, the throughput on the EPYC server was about 5% higher than on the MacBook Air. The EPYC system processed ~299k events per second, versus ~285k events per second on the Mac. This is a modest advantage for the server-grade CPU. In terms of average latency per event, both systems were on the order of just 3 microseconds (μs) – 3.18 μs on EPYC vs 3.33 μs on the Mac. This indicates that for the vast majority of order events, the processing time was extremely low on both platforms, reflecting the efficiency of the simulator’s single-threaded order book logic and data structures. The 95th percentile latency was identical at 3 μs on both, meaning 95% of all events were handled in 3 μs or less on both machines. This suggests that typical latency performance was nearly the same in both environments.
However, one notable difference was observed in the worst-case latency (max latency). The MacBook Air run experienced a single outlier event that took 5.134 milliseconds (5134 μs) to process, whereas the maximum latency on the EPYC server was only 76 μs. In other words, the MacBook Air exhibited a rare but much larger jitter/spike in latency.
Figure 2: Maximum observed latency on each platform. The MacBook Air run had a one-time latency spike of 5,134 μs (5.1 ms), dramatically higher than the EPYC server’s maximum of 76 μs. Aside from this outlier, both systems maintained ~3 μs typical latency (as indicated by the identical 95th-percentile). The EPYC server’s latency was more consistent with fewer extreme deviations.

<p align="center">
  <img src="imgs/fig2.png" alt="fig1" width="500">
</p>

The chart above highlights the disparity in tail latency. The MacBook Air (in a Docker container) likely suffered an occasional scheduling delay or resource contention that led to a 5 ms pause for one event. In contrast, the EPYC server (running on bare metal) provided much more consistent latency, with no event exceeding 76 μs. It’s important to note that apart from this single anomaly on the Mac, the overall latency profile was very similar between the two platforms (as reflected by the equal 95th percentile of 3 μs). This indicates that both systems handle the workload efficiently under normal conditions, but the server-grade hardware offers more stable real-time performance with fewer jitter outliers.
Analysis
Throughput and Latency Comparison: The 16-core AMD EPYC system achieved slightly better throughput and nearly identical average latency when compared to the MacBook Air. The ~5% throughput gain on EPYC can be attributed to its higher per-core performance and the absence of virtualization overhead. The AMD EPYC 4564P processor has 16 high-performance cores (32 threads with SMT) and runs at a base frequency of 4.5 GHz with boosts up to 5.7 GHz. This high clock speed and server-grade architecture give it a single-thread advantage, which is beneficial since each order book thread is primarily single-threaded for its workload. In contrast, the MacBook Air (hardware details unspecified, but likely a lower-power laptop CPU) operates at lower clock speeds and was further constrained by running inside a Docker container on macOS. Despite these differences, the MacBook Air’s performance was impressively close in terms of average latency and throughput, demonstrating that the exchange simulator is highly efficient. For the given workload (~0.5 million events over ~1.7 seconds), the laptop-class hardware was able to keep up remarkably well in the common case.
The primary performance differentiator was latency consistency. The EPYC server provided much more predictable latency, as evidenced by its low maximum latency. The MacBook Air’s single 5 ms outlier could be due to OS scheduling, Docker virtualization overhead, or CPU frequency throttling on the laptop. Such a pause could occur if the container was briefly de-scheduled by the host OS or if the MacBook Air shifted focus to another background task. In a high-frequency trading context, these occasional latency spikes on a consumer machine could be problematic, underscoring why dedicated server hardware (with tuned Linux kernels and isolated cores) is preferred for production deployments. The EPYC’s ability to handle the workload without significant jitter likely comes from a combination of its greater core count (allowing other processes/interrupts to run on different cores) and server-optimized scheduling.
It’s also worth noting that the test workload, while using multiple threads (one per ticker), may not have fully saturated all 16 cores of the EPYC processor. If the IEX data log contained only a limited number of active tickers or if one ticker dominated the event stream, the performance would be bounded by the single-thread speed for that busiest ticker. In such a scenario, the EPYC’s high clock speed gives it an edge, but not an overwhelming one – hence the ~5% throughput difference. The MacBook Air likely has fewer physical cores (and those cores possibly servicing both the container and macOS tasks), but if the bottleneck was one core handling the bulk of events, both systems were exercising mainly one core at a time. This explains why increasing from a laptop to a 16-core server did not multiply throughput dramatically, but it did improve consistency and ensured plenty of headroom for parallelism.
Resource Utilization: During the test, both systems handled ~300k events per second with average processing times of only a few microseconds per event. This implies that CPU usage per thread was high but not maxed out – there was still capacity for more events or more threads if available. The EPYC server, with many cores, could in theory handle many more ticker threads in parallel or a higher incoming message rate before saturating. The MacBook Air’s CPU would likely hit a ceiling sooner for aggregate throughput if more load or additional ticker streams were applied. The structured logging (writing price updates and trades to log files) did not visibly bottleneck the processing in these runs, but heavier I/O or slower disks could potentially impact performance on less powerful machines.
High Core Count Considerations
One of the expected advantages of server-grade hardware is the ability to scale up with a high number of CPU cores. Modern HFT platforms and market simulators can exploit many cores by handling more instruments or parallel tasks simultaneously. For instance, AMD’s current-generation EPYC processors offer configurations up to 96 cores on a single CPU, significantly higher than the 16-core chip used in our test. In theory, moving from 16 cores to 96 cores (a 6× increase) could allow the system to process far more events in parallel, provided the workload can be partitioned across independent threads (e.g. dozens of active tickers being processed concurrently). In our one-thread-per-ticker design, a 96-core machine would allow at least 96 independent order books to run in parallel, potentially scaling total throughput roughly linearly with core count. More cores also mean the OS can dedicate specific cores to handle background tasks or interrupts, minimizing their interference with the critical real-time threads, thus further improving latency consistency.
Other benefits of high-core-count architectures include larger combined cache and memory bandwidth. A 96-core EPYC system would typically have a higher total L3 cache and memory throughput, which can help when many threads are accessing market data and order books simultaneously. This can reduce contention for memory and keep more of the working dataset in fast caches, improving performance for memory-intensive scenarios.
Unfortunately, we could not test on a 96-core system as part of this benchmark. Access to such high-core-count servers is limited due to their cost and availability, and our testing environment was constrained to the 16-core EPYC server and the MacBook Air. Additionally, even if we had a 96-core machine, the IEX log replay might not fully utilize it unless the input data had enough parallelism (i.e. many active tickers). It remains an area for future exploration to see how well the exchange simulator scales on a larger server. We anticipate near-linear throughput scaling with core count up to the point of other bottlenecks (e.g. memory or I/O limits), but this hypothesis would need to be validated when appropriate hardware becomes available.
Conclusion
In summary, the custom HFT exchange simulator demonstrated excellent performance on both a consumer-grade laptop and a server-grade CPU. The 16-core AMD EPYC 4564P server processed the stream of ~509k events in 1.704 seconds (~299k events/s) with an average latency of 3.19 μs per event. The MacBook Air (Docker container) processed the same load in 1.788 seconds (~285k events/s) with an average latency of 3.33 μs. In typical operation (up to the 95th percentile), both platforms achieved ~3 μs latencies for order processing, highlighting the efficiency of the system’s design.
The key differences were observed in peak performance and consistency: the EPYC server had a slight throughput edge and maintained a low maximum latency of 76 μs, whereas the MacBook Air saw a one-off 5.1 ms latency spike. This outlier suggests that while a MacBook Air is capable of handling a realistic HFT load in a pinch (especially for development or testing), it cannot guarantee the same level of real-time consistency as dedicated server hardware. The server-grade platform offers more deterministic performance, likely thanks to its superior CPU scheduling, higher clock speeds, and greater core resources that reduce contention.
Overall, the HFT system scales well from a laptop to a multi-core server, and performance remains in the microsecond range on both. For production HFT deployments, the AMD EPYC server (or similar high-performance, multi-core systems) is recommended to achieve maximum throughput and ultra-low latency with minimal jitter. Looking ahead, tests on higher core count machines (e.g., 32-core, 64-core, or 96-core servers) would be valuable to confirm scalability. We expect that additional cores will allow the system to handle more parallel order books and higher event rates, although diminishing returns may appear if other factors (like memory bandwidth or the single-thread speed of each core) become bottlenecks. Nonetheless, this comparison has shown that our exchange simulator is efficient and that hardware differences primarily affect throughput and tail latency rather than fundamentally altering microsecond-level processing capability in the common case.




# Testing Latency & Throughput of Multithreaded Exchange in C++ on Different Architectures

<p align="center">
  <img src="imgs/Ratik_headshot.JPG" alt="Headshot" width="200">
</p>

**Ratik Iyer, SWE**

Hi, I'm Ratik, a sophomore at the University of Illinois Urbana-Champaign studying Statistics & Computer Science. I am passionate about quantitative development, low-latency systems, and high-performance computing. My primary language is C++, and I am also proficient in Python and R. I am particularly interested in software engineering roles at trading firms, where performance, optimization, and precision are critical. I am currently seeking Summer 2026 internship opportunities.

Email: ratiki2@illinois.edu  
LinkedIn: [https://www.linkedin.com/in/ratik-iyer-57058b17a/](https://www.linkedin.com/in/ratik-iyer-57058b17a/)  
Github: [https://github.com/ratikiyer](https://github.com/ratikiyer)

<p align="center">
  <img src="imgs/Kevin_headshot.JPG" alt="Headshot" width="200"/>
</p>

**Kevin Xu, SWE**

Hello, I am a sophomore studying Statistics + Computer Science. I am interested in statistical modeling, software engineering, and high frequency trading technology development. I am proficient in Python, C++, and C# and am interested in any opportunities for Summer 2026. 


Email: khx2@illinois.edu  
LinkedIn: [https://www.linkedin.com/in/kevinxu501/](https://www.linkedin.com/in/kevinxu501/)

<p align="center">
  <img src="imgs/Sidd_headshot.png" alt="Sidd_Headshot" width="200"/>
</p>

**Sidd Cheetancheri, SWE**

Hello, I am a sophomore studying Computer Science at UIUC. I am interested in machine learning (particularly transformer models) and computer networking. I am proficient in C++, Python, and C. I am interested in opportunities involving ML optimization or network solutions for Summer 2026.

Email: siddc2@illinois.edu  
LinkedIn: [https://www.linkedin.com/in/sidd-cheetancheri/](https://www.linkedin.com/in/sidd-cheetancheri/)

<p align="center">
  <img src="imgs/Anuraag_headshot.jpg" alt="Anuraag_Headshot" width="200"/>
</p>

**Anuraag Aravindan, SWE**

Hello, I am a sophomore studying Computer Science and Economics at UIUC. I am interested in quantitative trading, statistical modeling, and machine learning. I am proficient in Python, and C++. I am interested in opportunties as a trader at options market makers and hedge funds for both low and high touch desks. I am interested in any summer 2026 opportunities.

Email: anuraag6@illinois.edu  
LinkedIn: [https://www.linkedin.com/in/anuraag-aravindan/](https://www.linkedin.com/in/anuraag-aravindan/)

