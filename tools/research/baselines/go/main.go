// Cross-runtime baseline (Go host, cgo binding yalue/onnxruntime_go). Workload: tools/research/baselines/README.md
//
//	ORT_LIB=/path/libonnxruntime.so go run . <model.onnx> <ep> <batch> <warmup_s> <measure_s>
package main

import (
	"fmt"
	"os"
	"runtime"
	"sort"
	"strconv"
	"sync/atomic"
	"time"

	ort "github.com/yalue/onnxruntime_go"
)

var phase atomic.Int32 // 0 warm-up, 1 measuring, 2 stop
var seed int64 = 12345

func next() int64 {
	seed = (seed*1103515245 + 12345) % 2147483648
	return seed
}
func size() int { return 1024 + int((next()/256)%64513) }

func pct(s []float64, n int, p float64) float64 { return s[int(float64(n-1)*p)] }

func main() {
	model, ep := os.Args[1], os.Args[2]
	batch, _ := strconv.Atoi(os.Args[3])
	warmup, _ := strconv.Atoi(os.Args[4])
	measure, _ := strconv.Atoi(os.Args[5])

	ort.SetSharedLibraryPath(os.Getenv("ORT_LIB"))
	if err := ort.InitializeEnvironment(); err != nil {
		panic(err)
	}
	opts, err := ort.NewSessionOptions()
	if err != nil {
		panic(err)
	}
	if ep == "cuda" {
		co, err := ort.NewCUDAProviderOptions()
		if err != nil {
			panic(err)
		}
		if err := opts.AppendExecutionProviderCUDA(co); err != nil {
			panic(err)
		}
	}
	count := batch * 3 * 224 * 224
	in := make([]float32, count)
	for i := range in {
		in[i] = float32(float64(int64(i)*7919%1000) / 1000.0)
	}
	inT, err := ort.NewTensor(ort.NewShape(int64(batch), 3, 224, 224), in)
	if err != nil {
		panic(err)
	}
	outT, err := ort.NewEmptyTensor[float32](ort.NewShape(int64(batch), 1000))
	if err != nil {
		panic(err)
	}
	inputs, outputs, err := ort.GetInputOutputInfo(model)
	if err != nil {
		panic(err)
	}
	session, err := ort.NewAdvancedSession(model, []string{inputs[0].Name}, []string{outputs[0].Name},
		[]ort.Value{inT}, []ort.Value{outT}, opts)
	if err != nil {
		panic(err)
	}

	stalls := make([]float64, 0, 1_200_000)
	lat := make([]float64, 0, 400_000)
	var first []float32
	base := time.Now()
	nanos := func() int64 { return int64(time.Since(base)) }

	done := make(chan struct{}, 2)
	go func() { // allocator
		live := make([][]byte, 8192)
		for i := range live {
			live[i] = make([]byte, size())
		}
		prevEnd := nanos()
		k := int64(0)
		ph := int32(0)
		for ph < 2 {
			target := prevEnd + 100_000
			for nanos() < target {
			}
			sz := size()
			data := make([]byte, sz)
			data[0] = byte(k % 251)
			data[sz-1] = byte(k % 251)
			if k%16 == 0 {
				live[int(next())%8192] = data
			}
			end := nanos()
			if ph == 1 && len(stalls) < cap(stalls) {
				stalls = append(stalls, float64(end-target)/1000.0)
			}
			prevEnd = end
			k++
			if k&63 == 0 {
				p := phase.Load()
				if p != ph {
					ph = p
					if p == 1 {
						stalls = stalls[:0]
					}
				}
			}
		}
		done <- struct{}{}
	}()
	go func() { // inference
		ph := int32(0)
		for ph < 2 {
			t0 := nanos()
			if err := session.Run(); err != nil {
				panic(err)
			}
			t1 := nanos()
			if first == nil {
				first = append([]float32(nil), outT.GetData()...)
			}
			ph = phase.Load()
			if ph == 1 && len(lat) < cap(lat) {
				lat = append(lat, float64(t1-t0)/1e6)
			}
			if ph == 0 {
				lat = lat[:0]
			}
		}
		done <- struct{}{}
	}()

	time.Sleep(time.Duration(warmup) * time.Second)
	var m0, m1 runtime.MemStats
	runtime.ReadMemStats(&m0)
	w0 := time.Now()
	phase.Store(1)
	time.Sleep(time.Duration(measure) * time.Second)
	elapsed := time.Since(w0).Seconds()
	runtime.ReadMemStats(&m1)
	phase.Store(2)
	<-done
	<-done

	n, runs := len(stalls), len(lat)
	sort.Float64s(stalls)
	sort.Float64s(lat)
	sum := 0.0
	for _, v := range first {
		sum += float64(v)
	}
	best := 0
	for i := 0; i < 1000; i++ {
		if first[i] > first[best] {
			best = i
		}
	}
	gcs := m1.NumGC - m0.NumGC
	maxPause := uint64(0)
	for i := uint32(0); i < gcs && i < 256; i++ {
		p := m1.PauseNs[(m1.NumGC-1-i)%256]
		if p > maxPause {
			maxPause = p
		}
	}
	fmt.Printf("RESULT {\"stall_us\":{\"n\":%d,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"p999\":%.3f,\"max\":%.3f},"+
		"\"infer_ms\":{\"n\":%d,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f},\"img_s\":%.3f,"+
		"\"gc\":{\"num_gc\":%d,\"pause_total_ms\":%.3f,\"pause_max_ms\":%.3f,\"gogc\":\"%s\"},"+
		"\"checksum\":{\"sum\":%.6f,\"argmax0\":%d,\"v0\":%.6f,\"v1\":%.6f,\"v2\":%.6f}}\n",
		n, stalls[n/2], pct(stalls, n, .95), pct(stalls, n, .99), pct(stalls, n, .999), stalls[n-1],
		runs, lat[runs/2], pct(lat, runs, .95), pct(lat, runs, .99), lat[runs-1], float64(runs*batch)/elapsed,
		gcs, float64(m1.PauseTotalNs-m0.PauseTotalNs)/1e6, float64(maxPause)/1e6, os.Getenv("GOGC"),
		sum, best, first[0], first[1], first[2])
	os.Exit(0)
}
