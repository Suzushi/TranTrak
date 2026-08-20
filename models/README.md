# Diagnostic Models

The diagnostic executable expects three files in this directory:

- `face_detection_yunet_2023mar.onnx`
- `lm_model1_opt.onnx`
- `model_66.txt`

The current development checkout already contains compatible files under the
old repository. From a sibling checkout, copy them with:

```sh
cp ../HEADTracker/foxtrack-v2/assets_extra/models/face_detection_yunet_2023mar.onnx .
cp ../HEADTracker/assets/landmark_models/lm_model1_opt.onnx .
cp ../HEADTracker/assets/landmark_models/model_66.txt .
```

Model files are kept out of this repository until their redistribution terms
are confirmed. The executable also accepts explicit paths through
`--yunet`, `--landmark`, and `--face-model`.
