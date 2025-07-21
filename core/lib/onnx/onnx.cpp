#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>

#include <opencv2/opencv.hpp>

#include <vector>
#include <string>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <random>
#include <filesystem>

#ifdef _WIN32
namespace fs = std::filesystem;
#endif

std::vector<std::string> load_labels(const std::string name);

int main(int argc, char* argv[]) {
	// Initialize environment
	Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "Image Identifiction");

	// Initialize session options
	Ort::SessionOptions session_options;
	session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
	session_options.DisableMemPattern();

	// Get the OrtDmlApi
	OrtApi const& ortApi = Ort::GetApi();
	OrtDmlApi const* ortDmlApi = nullptr;
	ortApi.GetExecutionProviderApi("DML", ORT_API_VERSION, reinterpret_cast<void const**>(&ortDmlApi));
	
	// enable hardware acceleration
	OrtStatus* status = nullptr;
	status = ortDmlApi->SessionOptionsAppendExecutionProvider_DML(session_options, 0);

	if(status != nullptr) {
		// handle error
		Ort::GetApi().ReleaseStatus(status);
		std::cerr << "No inference results..." << std::endl;
		return 1;
	}

	// Create session
	Ort::Session session(env, L"resnet34-v2-7.onnx", session_options);

	// look for NPU device
	auto execution_providers = Ort::GetAvailableProviders();
	for(size_t i = 0; i < execution_providers.size(); ++i) {
		auto provider_name = execution_providers[i];
		std::cout << "Provider: id=" << i << ", name='" << provider_name << "'" << std::endl;
	}

	// Get input info
	Ort::AllocatorWithDefaultOptions allocator;
	size_t num_input_nodes = session.GetInputCount();
	std::vector<const char*> input_node_names;
	std::vector<int64_t> input_node_dims;

	for(size_t i = 0; i < num_input_nodes; ++i) {
		Ort::AllocatedStringPtr input_name = session.GetInputNameAllocated(i, allocator);
		input_node_names.push_back(input_name.get());

		// get input type info
		Ort::TypeInfo type_info = session.GetInputTypeInfo(i);
		auto tensor_info = type_info.GetTensorTypeAndShapeInfo();

		// get input shapes
		input_node_dims = tensor_info.GetShape();
	}

	// image directory
	std::string imageDir("data/test2017/");
	int sample_size = 5000; // how many random images to pick

	std::vector<std::string> all_images;

	// Gather all .jpg files in the directory
	for(const auto& entry : fs::directory_iterator(imageDir)) {
		if(entry.is_regular_file()) {
			std::string path = entry.path().string();
			// check extension case-insensitively
			if(entry.path().extension() == ".jpg" || entry.path().extension() == ".jpg") {
				all_images.push_back(path);
			}
		}
	}

	// Shuffle the vector randomly
	std::random_device rd;
	std::mt19937 g(rd());
	std::shuffle(all_images.begin(), all_images.end(), g);

	// Pick the first n images (or fewer if not enough)
	int pickCount = std::min(sample_size, (int)all_images.size());
	std::vector<std::string> selected_images(all_images.begin(), all_images.begin() + pickCount);

	// Open log file
	std::ofstream log_file("results_dml.csv");

	log_file << "inf_sample,inf_time_ms,inf_conf_pct,image_index,image_name,image_label\n";

	// Print them out
	std::vector<std::string> label_names = load_labels(imageDir + "labels.txt");
	for(size_t i = 0; i < selected_images.size(); ++i) {
		// get read in image
		auto selected_image = selected_images[i];
		cv::Mat image = cv::imread(selected_image); // BGR format

		// Start timing
		auto start = std::chrono::high_resolution_clock::now();

		// Resize to 224x224
		cv::Mat resized;
		cv::resize(image, resized, cv::Size(224, 224));

		// Convert to float and normalize to 0–1
		cv::Mat image_float;
		resized.convertTo(image_float, CV_32F, 1.0f / 255.0f);

		// Split channels and reorder to NCHW
		std::vector<cv::Mat> channels(3);
		cv::split(image_float, channels); // BGR order
		// If model expects RGB, swap channels[0] and channels[2]
		std::swap(channels[0], channels[2]);

		// Flatten into a single vector in CHW order
		std::vector<float> input_tensor_values;
		for(int c = 0; c < 3; c++) {
			input_tensor_values.insert(input_tensor_values.end(),
				(float*)channels[c].datastart,
				(float*)channels[c].dataend);
		}

		// Define the shape (NCHW)
		std::vector<int64_t> input_shape = { 1, 3, 224, 224 };

		// Create input tensor once and reuse
		Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(
			OrtAllocatorType::OrtArenaAllocator, OrtMemTypeDefault);

		Ort::AllocatedStringPtr input_name_ptr = session.GetInputNameAllocated(0, allocator);
		Ort::AllocatedStringPtr output_name_ptr = session.GetOutputNameAllocated(0, allocator);

		const char* input_names[] = { input_name_ptr.get() };
		const char* output_names[] = { output_name_ptr.get() };

		Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
			memory_info,
			input_tensor_values.data(),
			input_tensor_values.size(),
			input_shape.data(),
			input_shape.size()
		);

		// inference query
		auto outputs = session.Run(
			Ort::RunOptions { nullptr },
			input_names,
			&input_tensor,
			1,
			output_names,
			1
		);

		if(outputs.empty()) {
			std::cerr << "No inference results..." << std::endl;
			Ort::GetApi().ReleaseStatus(status);
			return 1;
		}

		// Calculate duration in milliseconds
		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

		float* output_data = outputs.front().GetTensorMutableData<float>();
		Ort::Value& output_tensor = outputs.front();

		// Get shape info
		Ort::TensorTypeAndShapeInfo shape_info = output_tensor.GetTensorTypeAndShapeInfo();

		// Number of elements
		size_t output_len = shape_info.GetElementCount();

		/*
		for (size_t j = 0; j < output_len; ++j) {
			 std::cout << "Run: " << j << " output: " << output_data[j] << std::endl;
		}
		*/

		std::vector<float> probs(output_len);
		float max_logit = output_data[0];

		// find max for numerical stability
		for(size_t j = 1; j < output_len; j++) {
			if(output_data[j] > max_logit) {
				max_logit = output_data[j];
			}
		}

		// compute exp(logit - max_logit)
		float sum_exp = 0.0f;
		for(size_t j = 0; j < output_len; j++) {
			probs[j] = std::exp(output_data[j] - max_logit);
			sum_exp += probs[j];
		}

		// normalize
		for(size_t j = 0; j < output_len; j++) {
			probs[j] /= sum_exp;
		}

		size_t image_index = 0;
		float top_confidence = probs[0];

		for(size_t j = 1; j < output_len; j++) {
			if(probs[j] > top_confidence) {
				top_confidence = probs[j];
				image_index = j;
			}
		}

		// only print high confidence results
		if(top_confidence > 0.7f) {
			log_file << i << "," << duration << "," << top_confidence << "," << image_index
				<< "," << selected_image << "," << label_names[image_index] << "\n";
		}
	}

	log_file.close();

	std::cout << "Fin." << std::endl;

	return 0;
}

// Datasets: https://cocodataset.org/#download
// 2017 Train/Val/Test: http://images.cocodataset.org/zips/train2017.zip
std::vector<std::string> load_labels(const std::string name) {
	std::vector<std::string> labels;

	std::ifstream file_in(name);

	std::string label;
	while(std::getline(file_in, label)) {
		labels.push_back(label);
	}

	file_in.close();

	return labels;
}
