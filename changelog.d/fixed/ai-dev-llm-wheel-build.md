- **The `vmaf-train` (`ai/`) and `vmaf-dev-llm` wheels build again.** Their
  `force-include` repeated directories the packages already ship, which
  hatchling 1.32 refuses ("A second file is being added to the wheel
  archive"). The wheels still carry the dataset manifests, the training
  configurations and the prompt templates.
